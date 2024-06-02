#pragma once

#include "rendering/postprocessing_pipeline.h"
#include "rendering/render_pipeline.h"
#include "scene_graph/components/perspective_camera.h"
#include "vulkan_sample.h"

class WRenderer : public vkb::VulkanSample<vkb::BindingType::C>
{
  public:
	WRenderer();

	virtual ~WRenderer() = default;

	virtual bool prepare(const vkb::ApplicationOptions &options) override;

	virtual void update(float delta_time) override;

	virtual void draw(vkb::CommandBuffer &command_buffer, vkb::RenderTarget &render_target) override;

	void draw_gui() override;

  private:
	vkb::sg::PerspectiveCamera *camera{nullptr};

	virtual void prepare_render_context() override;

	std::unique_ptr<vkb::RenderTarget> create_render_target(vkb::core::Image &&swapchain_image);

	/**
	 * @brief Scene pipeline
	 *        Render and light the scene (optionally using MSAA)
	 */
	std::unique_ptr<vkb::RenderPipeline> scene_pipeline{};

	void update_scene_subpass();

	/**
	 * @brief Update MSAA options and accordingly set the load/store
	 *        attachment operations for the renderpasses
	 *        This will trigger a swapchain recreation
	 */
	void update_pipelines();

	/**
	 * @brief Update pipelines given that there will be a single
	 *        renderpass for rendering the scene and GUI only
	 */
	void update_for_scene_only();

	/**
	 * @brief Enables MSAA if set to more than 1 sample per pixel
	 *        (e.g. sample count 4 enables 4X MSAA)
	 */
	VkSampleCountFlagBits sample_count{VK_SAMPLE_COUNT_1_BIT};

	/**
	 * @brief Sets the single-sampled output_attachment as the output attachment,
	 *        disables color resolve and updates the load/store operations of
	 *        color attachments
	 */
	void use_singlesampled_color(std::unique_ptr<vkb::Subpass> &subpass, std::vector<vkb::LoadStoreInfo> &load_store, uint32_t output_attachment);

	/**
	 * @brief Disables depth writeback resolve and updates the load/store operations of
	 *        the depth resolve attachment
	 */
	void disable_depth_writeback_resolve(std::unique_ptr<vkb::Subpass> &subpass, std::vector<vkb::LoadStoreInfo> &load_store);

	/* Helpers for managing attachments */

	uint32_t i_swapchain{0};

	uint32_t i_depth{0};

	std::vector<uint32_t> color_atts{};

	std::vector<uint32_t> depth_atts{};

	std::vector<vkb::LoadStoreInfo> scene_load_store{};

	bool refresh_shader = false;

	std::string shading_model;
};

std::unique_ptr<vkb::VulkanSample<vkb::BindingType::C>> create_w_renderer();
