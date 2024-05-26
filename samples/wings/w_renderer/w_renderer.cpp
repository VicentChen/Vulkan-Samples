#include "w_renderer.h"

#include "common/vk_common.h"
#include "filesystem/legacy.h"
#include "gltf_loader.h"
#include "gui.h"

#include "rendering/postprocessing_renderpass.h"
#include "rendering/subpasses/forward_subpass.h"
#include "stats/stats.h"

WRenderer::WRenderer()
{
	// Extension of interest in this sample (optional)
	add_device_extension(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME, true);

	// Extension dependency requirements (given that instance API version is 1.0.0)
	add_instance_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, true);
	add_device_extension(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME, true);
	add_device_extension(VK_KHR_MAINTENANCE2_EXTENSION_NAME, true);
	add_device_extension(VK_KHR_MULTIVIEW_EXTENSION_NAME, true);

	auto &config = get_configuration();
}

bool WRenderer::prepare(const vkb::ApplicationOptions &options)
{
	if (!VulkanSample::prepare(options))
	{
		return false;
	}

//	load_scene("scenes/space_module/SpaceModule.gltf");
	load_scene("scenes/geosphere.gltf");

	auto &camera_node = vkb::add_free_camera(get_scene(), "main_camera", get_render_context().get_surface_extent());
	camera            = dynamic_cast<vkb::sg::PerspectiveCamera *>(&camera_node.get_component<vkb::sg::Camera>());

	vkb::ShaderSource scene_vs("wings/phong.vert");
	vkb::ShaderSource scene_fs("wings/phong.frag");
	auto              scene_subpass = std::make_unique<vkb::ForwardSubpass>(get_render_context(), std::move(scene_vs), std::move(scene_fs), get_scene(), *camera);
	scene_pipeline                  = std::make_unique<vkb::RenderPipeline>();
	scene_pipeline->add_subpass(std::move(scene_subpass));

	update_pipelines();

	get_stats().request_stats({vkb::StatIndex::frame_times,
	                           vkb::StatIndex::gpu_ext_read_bytes,
	                           vkb::StatIndex::gpu_ext_write_bytes});

	create_gui(*window, &get_stats());

	return true;
}

void WRenderer::prepare_render_context()
{
	get_render_context().prepare(1, std::bind(&WRenderer::create_render_target, this, std::placeholders::_1));
}

std::unique_ptr<vkb::RenderTarget> WRenderer::create_render_target(vkb::core::Image &&swapchain_image)
{
	auto &device = swapchain_image.get_device();
	auto &extent = swapchain_image.get_extent();

	auto              depth_format        = vkb::get_suitable_depth_format(device.get_gpu().get_handle());
	VkImageUsageFlags depth_usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	// Depth attachments are transient
	depth_usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

	vkb::core::Image depth_image{device,
	                             extent,
	                             depth_format,
	                             depth_usage,
	                             VMA_MEMORY_USAGE_GPU_ONLY,
	                             sample_count};

	scene_load_store.clear();
	std::vector<vkb::core::Image> images;

	// Attachment 0 - Swapchain
	// Used by the scene renderpass if postprocessing is disabled
	// Used by the postprocessing renderpass if postprocessing is enabled
	i_swapchain = 0;
	images.push_back(std::move(swapchain_image));
	scene_load_store.push_back({VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE});

	// Attachment 1 - Depth
	// Always used by the scene renderpass, may or may not be multisampled
	i_depth = 1;
	images.push_back(std::move(depth_image));
	scene_load_store.push_back({VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE});

	color_atts = {i_swapchain};
	depth_atts = {i_depth};

	return std::make_unique<vkb::RenderTarget>(std::move(images));
}

void WRenderer::update(float delta_time)
{
//	update_pipelines(); // Only update when required.
	if (refresh_shader){
		vkb::ShaderSource scene_vs("wings/phong.vert");
		vkb::ShaderSource scene_fs("wings/phong.frag");
		auto              scene_subpass = std::make_unique<vkb::ForwardSubpass>(get_render_context(), std::move(scene_vs), std::move(scene_fs), get_scene(), *camera);
		scene_pipeline                  = std::make_unique<vkb::RenderPipeline>();
		scene_pipeline->add_subpass(std::move(scene_subpass));
		update_pipelines();

		refresh_shader = false;
	}

	VulkanSample::update(delta_time);
}

void WRenderer::update_pipelines()
{
	update_for_scene_only();

	// Default swapchain usage flags
	std::set<VkImageUsageFlagBits> swapchain_usage = {VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT};

	get_device().wait_idle();

	get_render_context().update_swapchain(swapchain_usage);
}

void WRenderer::update_for_scene_only()
{
	auto &scene_subpass = scene_pipeline->get_active_subpass();
	scene_subpass->set_sample_count(sample_count);

	// Render color to the swapchain
	use_singlesampled_color(scene_subpass, scene_load_store, i_swapchain);

	// Depth attachment is transient, it will not be needed after the renderpass
	// If it is multisampled, there is no need to resolve it
	scene_load_store[i_depth].store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	disable_depth_writeback_resolve(scene_subpass, scene_load_store);

	// Update the scene renderpass
	scene_pipeline->set_load_store(scene_load_store);
}

void WRenderer::use_singlesampled_color(std::unique_ptr<vkb::Subpass> &subpass, std::vector<vkb::LoadStoreInfo> &load_store, uint32_t output_attachment)
{
	// Render to a single-sampled attachment
	subpass->set_output_attachments({output_attachment});
	load_store[output_attachment].store_op = VK_ATTACHMENT_STORE_OP_STORE;

	// Disable writeback resolve
	subpass->set_color_resolve_attachments({});
}

void WRenderer::disable_depth_writeback_resolve(std::unique_ptr<vkb::Subpass> &subpass, std::vector<vkb::LoadStoreInfo> &load_store)
{
	// Disable writeback resolve
	subpass->set_depth_stencil_resolve_attachment(VK_ATTACHMENT_UNUSED);
	subpass->set_depth_stencil_resolve_mode(VK_RESOLVE_MODE_NONE);
}

void WRenderer::draw(vkb::CommandBuffer &command_buffer, vkb::RenderTarget &render_target)
{
	auto &views = render_target.get_views();

	auto swapchain_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	{
		vkb::ImageMemoryBarrier memory_barrier{};
		memory_barrier.old_layout      = VK_IMAGE_LAYOUT_UNDEFINED;
		memory_barrier.new_layout      = swapchain_layout;
		memory_barrier.src_access_mask = 0;
		memory_barrier.dst_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		memory_barrier.src_stage_mask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		memory_barrier.dst_stage_mask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		for (auto &i_color : color_atts)
		{
			assert(i_color < views.size());
			command_buffer.image_memory_barrier(views[i_color], memory_barrier);
			render_target.set_layout(i_color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		}
	}

	{
		vkb::ImageMemoryBarrier memory_barrier{};
		memory_barrier.old_layout      = VK_IMAGE_LAYOUT_UNDEFINED;
		memory_barrier.new_layout      = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		memory_barrier.src_access_mask = 0;
		memory_barrier.dst_access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		memory_barrier.src_stage_mask  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		memory_barrier.dst_stage_mask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;


		for (auto &i_depth : depth_atts)
		{
			assert(i_depth < views.size());
			command_buffer.image_memory_barrier(views[i_depth], memory_barrier);
			render_target.set_layout(i_depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		}
	}

	auto &extent = render_target.get_extent();

	VkViewport viewport{};
	viewport.width    = static_cast<float>(extent.width);
	viewport.height   = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	command_buffer.set_viewport(0, {viewport});

	VkRect2D scissor{};
	scissor.extent = extent;
	command_buffer.set_scissor(0, {scissor});

	scene_pipeline->draw(command_buffer, render_target);

	if (has_gui())
	{
		get_gui().draw(command_buffer);
	}

	command_buffer.end_render_pass();

	{
		// Prepare swapchain for presentation
		vkb::ImageMemoryBarrier memory_barrier{};
		memory_barrier.old_layout      = swapchain_layout;
		memory_barrier.new_layout      = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		memory_barrier.src_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		memory_barrier.src_stage_mask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		memory_barrier.dst_stage_mask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

		assert(i_swapchain < views.size());
		command_buffer.image_memory_barrier(views[i_swapchain], memory_barrier);
	}
}

void WRenderer::draw_gui()
{
	get_gui().show_options_window([this]() {
		if (ImGui::Button("Refresh shader")) {
			this->refresh_shader = true;
			LOGW("Refresh shader");
		}
	});
}

std::unique_ptr<vkb::VulkanSample<vkb::BindingType::C>> create_w_renderer()
{
	return std::make_unique<WRenderer>();
}
