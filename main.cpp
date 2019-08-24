#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <array>
#include <SDL.h>
#include <SDL_syswm.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include "spirv_shaders_embedded_spv.h"

#define CHECK_VULKAN(FN) \
	{ \
		VkResult r = FN; \
		if (r != VK_SUCCESS) {\
			std::cout << #FN << " failed\n" << std::flush; \
			throw std::runtime_error(#FN " failed!");  \
		} \
	}

const std::array<float, 9> triangle_verts = {
	0.0, -0.5, 0.0,
	0.5, 0.5, 0.0,
	-0.5, 0.5, 0.0
};

const std::array<uint32_t, 3> triangle_indices = { 0, 1, 2 };

// See https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#acceleration-structure-instance
struct VkGeometryInstanceNV {
	float transform[12];
	uint32_t instance_custom_index : 24;
	uint32_t mask : 8;
	uint32_t instance_offset : 24;
	uint32_t flags : 8;
	uint64_t acceleration_structure_handle;
};

PFN_vkCreateAccelerationStructureNV vkCreateAccelerationStructure;
PFN_vkDestroyAccelerationStructureNV vkDestroyAccelerationStructure;
PFN_vkBindAccelerationStructureMemoryNV vkBindAccelerationStructureMemory;
PFN_vkGetAccelerationStructureHandleNV vkGetAccelerationStructureHandle;
PFN_vkGetAccelerationStructureMemoryRequirementsNV vkGetAccelerationStructureMemoryRequirements;
PFN_vkCmdBuildAccelerationStructureNV vkCmdBuildAccelerationStructure;
PFN_vkCreateRayTracingPipelinesNV vkCreateRayTracingPipelines;
PFN_vkGetRayTracingShaderGroupHandlesNV vkGetRayTracingShaderGroupHandles;
PFN_vkCmdTraceRaysNV vkCmdTraceRays;

uint32_t get_memory_type_index(uint32_t type_filter, VkMemoryPropertyFlags props, const VkPhysicalDeviceMemoryProperties &mem_props) {
	for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
		if (type_filter & (1 << i) && (mem_props.memoryTypes[i].propertyFlags & props) == props) {
			return i;
		}
	}
	throw std::runtime_error("failed to find appropriate memory");
}

void setup_vulkan_rtx_fcns(VkDevice &device) {
	vkCreateAccelerationStructure = reinterpret_cast<PFN_vkCreateAccelerationStructureNV>(vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureNV"));
	vkDestroyAccelerationStructure = reinterpret_cast<PFN_vkDestroyAccelerationStructureNV>(vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureNV"));
	vkBindAccelerationStructureMemory = reinterpret_cast<PFN_vkBindAccelerationStructureMemoryNV>(vkGetDeviceProcAddr(device, "vkBindAccelerationStructureMemoryNV"));
	vkGetAccelerationStructureHandle = reinterpret_cast<PFN_vkGetAccelerationStructureHandleNV>(vkGetDeviceProcAddr(device, "vkGetAccelerationStructureHandleNV"));
	vkGetAccelerationStructureMemoryRequirements = reinterpret_cast<PFN_vkGetAccelerationStructureMemoryRequirementsNV>(vkGetDeviceProcAddr(device, "vkGetAccelerationStructureMemoryRequirementsNV"));
	vkCmdBuildAccelerationStructure = reinterpret_cast<PFN_vkCmdBuildAccelerationStructureNV>(vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructureNV"));
	vkCreateRayTracingPipelines = reinterpret_cast<PFN_vkCreateRayTracingPipelinesNV>(vkGetDeviceProcAddr(device, "vkCreateRayTracingPipelinesNV"));
	vkGetRayTracingShaderGroupHandles = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesNV>(vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesNV"));
	vkCmdTraceRays = reinterpret_cast<PFN_vkCmdTraceRaysNV>(vkGetDeviceProcAddr(device, "vkCmdTraceRaysNV"));
}

int win_width = 1280;
int win_height = 720;

int main(int argc, const char **argv) {
	if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
		std::cerr << "Failed to init SDL: " << SDL_GetError() << "\n";
		return -1;
	}

	SDL_Window* window = SDL_CreateWindow("SDL2 + Vulkan",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_width, win_height, 0);
	
	{
		uint32_t extension_count = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
		std::cout << "num extensions: " << extension_count << "\n";
		std::vector<VkExtensionProperties> extensions(extension_count, VkExtensionProperties{});
		vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data());
		std::cout << "Available extensions:\n";
		for (const auto& e : extensions) {
			std::cout << e.extensionName << "\n";
		}
	}

	const std::array<const char*, 1> validation_layers = {
		"VK_LAYER_KHRONOS_validation"
	};

	// Make the Vulkan Instance
	VkInstance vk_instance = VK_NULL_HANDLE;
	{
		VkApplicationInfo app_info = {};
		app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app_info.pApplicationName = "SDL2 + Vulkan";
		app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		app_info.pEngineName = "None";
		app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		app_info.apiVersion = VK_API_VERSION_1_1;

		const std::array<const char*, 2> extension_names = {
			VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
		};

		VkInstanceCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		create_info.pApplicationInfo = &app_info;
		create_info.enabledExtensionCount = extension_names.size();
		create_info.ppEnabledExtensionNames = extension_names.data();
		create_info.enabledLayerCount = validation_layers.size();
		create_info.ppEnabledLayerNames = validation_layers.data();

		CHECK_VULKAN(vkCreateInstance(&create_info, nullptr, &vk_instance));
	}

	VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
	{
		SDL_SysWMinfo wm_info;
		SDL_VERSION(&wm_info.version);
		SDL_GetWindowWMInfo(window, &wm_info);

		VkWin32SurfaceCreateInfoKHR create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		create_info.hwnd = wm_info.info.win.window;
		create_info.hinstance = wm_info.info.win.hinstance;
		CHECK_VULKAN(vkCreateWin32SurfaceKHR(vk_instance, &create_info, nullptr, &vk_surface));
	}

	VkPhysicalDevice vk_physical_device = VK_NULL_HANDLE;
	{
		uint32_t device_count = 0;
		vkEnumeratePhysicalDevices(vk_instance, &device_count, nullptr);
		std::cout << "Found " << device_count << " devices\n";
		std::vector<VkPhysicalDevice> devices(device_count, VkPhysicalDevice{});
		vkEnumeratePhysicalDevices(vk_instance, &device_count, devices.data());

		const bool has_discrete_gpu = std::find_if(devices.begin(), devices.end(),
			[](const VkPhysicalDevice& d) {
				VkPhysicalDeviceProperties properties;
				vkGetPhysicalDeviceProperties(d, &properties);
				return properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
			}) != devices.end();

		for (const auto &d : devices) {
			VkPhysicalDeviceProperties properties;
			VkPhysicalDeviceFeatures features;
			vkGetPhysicalDeviceProperties(d, &properties);
			vkGetPhysicalDeviceFeatures(d, &features);	
			std::cout << properties.deviceName << "\n";

			// Check for RTX support
			uint32_t extension_count = 0;
			vkEnumerateDeviceExtensionProperties(d, nullptr, &extension_count, nullptr);
			std::cout << "num extensions: " << extension_count << "\n";
			std::vector<VkExtensionProperties> extensions(extension_count, VkExtensionProperties{});
			vkEnumerateDeviceExtensionProperties(d, nullptr, &extension_count, extensions.data());
			std::cout << "Device available extensions:\n";
			for (const auto& e : extensions) {
				std::cout << e.extensionName << "\n";
			}

			if (has_discrete_gpu && properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
				vk_physical_device = d;
				break;
			} else if (!has_discrete_gpu && properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
				vk_physical_device = d;
				break;
			}
		}
	}

	VkDevice vk_device = VK_NULL_HANDLE;
	VkQueue vk_queue = VK_NULL_HANDLE;
	uint32_t graphics_queue_index = -1;
	{
		uint32_t num_queue_families = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(vk_physical_device, &num_queue_families, nullptr);
		std::vector<VkQueueFamilyProperties> family_props(num_queue_families, VkQueueFamilyProperties{});
		vkGetPhysicalDeviceQueueFamilyProperties(vk_physical_device, &num_queue_families, family_props.data());
		for (uint32_t i = 0; i < num_queue_families; ++i) {
			// We want present and graphics on the same queue (kind of assume this will be supported on any discrete GPU)
			VkBool32 present_support = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(vk_physical_device, i, vk_surface, &present_support);
			if (present_support && (family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
				graphics_queue_index = i;
			}
		}
		std::cout << "Graphics queue is " << graphics_queue_index << "\n";
		const float queue_priority = 1.f;

		VkDeviceQueueCreateInfo queue_create_info = {};
		queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_create_info.queueFamilyIndex = graphics_queue_index;
		queue_create_info.queueCount = 1;
		queue_create_info.pQueuePriorities = &queue_priority;

		VkPhysicalDeviceFeatures device_features = {};

		const std::array<const char*, 3> device_extensions = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_NV_RAY_TRACING_EXTENSION_NAME,
			VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME
		};

		VkDeviceCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		create_info.queueCreateInfoCount = 1;
		create_info.pQueueCreateInfos = &queue_create_info;
		create_info.enabledLayerCount = validation_layers.size();
		create_info.ppEnabledLayerNames = validation_layers.data();
		create_info.enabledExtensionCount = device_extensions.size();
		create_info.ppEnabledExtensionNames = device_extensions.data();
		create_info.pEnabledFeatures = &device_features;
		CHECK_VULKAN(vkCreateDevice(vk_physical_device, &create_info, nullptr, &vk_device));
		setup_vulkan_rtx_fcns(vk_device);

		vkGetDeviceQueue(vk_device, graphics_queue_index, 0, &vk_queue);
	}

	VkPhysicalDeviceMemoryProperties mem_props = {};
	vkGetPhysicalDeviceMemoryProperties(vk_physical_device, &mem_props);

	// Query info about raytracing capabilities, shader header size, etc.
	VkPhysicalDeviceRayTracingPropertiesNV raytracing_props = {};
	{
		raytracing_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV;
		VkPhysicalDeviceProperties2 props = {};
		props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		props.pNext = &raytracing_props;
		props.properties = {};
		vkGetPhysicalDeviceProperties2(vk_physical_device, &props);

		std::cout << "Raytracing props:\n"
			<< "max recursion depth: " << raytracing_props.maxRecursionDepth
			<< "\nSBT handle size: " << raytracing_props.shaderGroupHandleSize
			<< "\nShader group base align: " << raytracing_props.shaderGroupBaseAlignment << "\n";
	}

	// Setup swapchain, assume a real GPU so don't bother querying the capabilities, just get what we want
	VkExtent2D swapchain_extent = {};
	swapchain_extent.width = win_width;
	swapchain_extent.height = win_height;
	const VkFormat swapchain_img_format = VK_FORMAT_B8G8R8A8_UNORM;

	VkSwapchainKHR vk_swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> swapchain_images;
	std::vector<VkImageView> swapchain_image_views;
	{
		VkSwapchainCreateInfoKHR create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		create_info.surface = vk_surface;
		create_info.minImageCount = 2;
		create_info.imageFormat = swapchain_img_format;
		create_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		create_info.imageExtent = swapchain_extent;
		create_info.imageArrayLayers = 1;
		create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		// We only have 1 queue
		create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
		create_info.clipped = true;
		create_info.oldSwapchain = VK_NULL_HANDLE;
		CHECK_VULKAN(vkCreateSwapchainKHR(vk_device, &create_info, nullptr, &vk_swapchain));

		// Get the swap chain images
		uint32_t num_swapchain_imgs = 0;
		vkGetSwapchainImagesKHR(vk_device, vk_swapchain, &num_swapchain_imgs, nullptr);
		swapchain_images.resize(num_swapchain_imgs);
		vkGetSwapchainImagesKHR(vk_device, vk_swapchain, &num_swapchain_imgs, swapchain_images.data());

		for (const auto &img : swapchain_images) {
			VkImageViewCreateInfo view_create_info = {};
			view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			view_create_info.image = img;
			view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view_create_info.format = swapchain_img_format;

			view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

			view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			view_create_info.subresourceRange.baseMipLevel = 0;
			view_create_info.subresourceRange.levelCount = 1;
			view_create_info.subresourceRange.baseArrayLayer = 0;
			view_create_info.subresourceRange.layerCount = 1;

			VkImageView img_view;
			CHECK_VULKAN(vkCreateImageView(vk_device, &view_create_info, nullptr, &img_view));
			swapchain_image_views.push_back(img_view);
		}
	}

	// Build the pipeline
	VkPipelineLayout vk_pipeline_layout;
	VkRenderPass vk_render_pass;
	VkPipeline vk_pipeline;
	{
		VkShaderModule vertex_shader_module = VK_NULL_HANDLE;

		VkShaderModuleCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		create_info.codeSize = sizeof(vert_spv);
		create_info.pCode = vert_spv;
		CHECK_VULKAN(vkCreateShaderModule(vk_device, &create_info, nullptr, &vertex_shader_module));
		
		VkPipelineShaderStageCreateInfo vertex_stage = {};
		vertex_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertex_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertex_stage.module = vertex_shader_module;
		vertex_stage.pName = "main";

		VkShaderModule fragment_shader_module = VK_NULL_HANDLE;
		create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		create_info.codeSize = sizeof(frag_spv);
		create_info.pCode = frag_spv;
		CHECK_VULKAN(vkCreateShaderModule(vk_device, &create_info, nullptr, &fragment_shader_module));

		VkPipelineShaderStageCreateInfo fragment_stage = {};
		fragment_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragment_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragment_stage.module = fragment_shader_module;
		fragment_stage.pName = "main";

		std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = { vertex_stage, fragment_stage };

		// Vertex data hard-coded in vertex shader
		VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
		vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertex_input_info.vertexBindingDescriptionCount = 0;
		vertex_input_info.vertexAttributeDescriptionCount = 0;

		// Primitive type
		VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
		input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		input_assembly.primitiveRestartEnable = VK_FALSE;

		// Viewport config
		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = win_width;
		viewport.height = win_height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		// Scissor rect config
		VkRect2D scissor = {};
		scissor.offset.x = 0;
		scissor.offset.y = 0;
		scissor.extent = swapchain_extent;

		VkPipelineViewportStateCreateInfo viewport_state_info = {};
		viewport_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewport_state_info.viewportCount = 1;
		viewport_state_info.pViewports = &viewport;
		viewport_state_info.scissorCount = 1;
		viewport_state_info.pScissors = &scissor;

		VkPipelineRasterizationStateCreateInfo rasterizer_info = {};
		rasterizer_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer_info.depthClampEnable = VK_FALSE;
		rasterizer_info.rasterizerDiscardEnable = VK_FALSE;
		rasterizer_info.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer_info.lineWidth = 1.f;
		rasterizer_info.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
		rasterizer_info.depthBiasEnable = VK_FALSE;

		VkPipelineMultisampleStateCreateInfo multisampling = {};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState blend_mode = {};
		blend_mode.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blend_mode.blendEnable = VK_FALSE;

		VkPipelineColorBlendStateCreateInfo blend_info = {};
		blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blend_info.logicOpEnable = VK_FALSE;
		blend_info.attachmentCount = 1;
		blend_info.pAttachments = &blend_mode;

		VkPipelineLayoutCreateInfo pipeline_info = {};
		pipeline_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		CHECK_VULKAN(vkCreatePipelineLayout(vk_device, &pipeline_info, nullptr, &vk_pipeline_layout));

		VkAttachmentDescription color_attachment = {};
		color_attachment.format = swapchain_img_format;
		color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference color_attachment_ref = {};
		color_attachment_ref.attachment = 0;
		color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &color_attachment_ref;

		VkRenderPassCreateInfo render_pass_info = {};
		render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		render_pass_info.attachmentCount = 1;
		render_pass_info.pAttachments = &color_attachment;
		render_pass_info.subpassCount = 1;
		render_pass_info.pSubpasses = &subpass;
		CHECK_VULKAN(vkCreateRenderPass(vk_device, &render_pass_info, nullptr, &vk_render_pass));

		VkGraphicsPipelineCreateInfo graphics_pipeline_info = {};
		graphics_pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		graphics_pipeline_info.stageCount = 2;
		graphics_pipeline_info.pStages = shader_stages.data();
		graphics_pipeline_info.pVertexInputState = &vertex_input_info;
		graphics_pipeline_info.pInputAssemblyState = &input_assembly;
		graphics_pipeline_info.pViewportState = &viewport_state_info;
		graphics_pipeline_info.pRasterizationState = &rasterizer_info;
		graphics_pipeline_info.pMultisampleState = &multisampling;
		graphics_pipeline_info.pColorBlendState = &blend_info;
		graphics_pipeline_info.layout = vk_pipeline_layout;
		graphics_pipeline_info.renderPass = vk_render_pass;
		graphics_pipeline_info.subpass = 0;
		CHECK_VULKAN(vkCreateGraphicsPipelines(vk_device, VK_NULL_HANDLE, 1, &graphics_pipeline_info, nullptr, &vk_pipeline));

		vkDestroyShaderModule(vk_device, vertex_shader_module, nullptr);
		vkDestroyShaderModule(vk_device, fragment_shader_module, nullptr);
	}

	// Setup framebuffers
	std::vector<VkFramebuffer> framebuffers;
	for (const auto &v : swapchain_image_views) {
		std::array<VkImageView, 1> attachments = { v };
		VkFramebufferCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		create_info.renderPass = vk_render_pass;
		create_info.attachmentCount = 1;
		create_info.pAttachments = attachments.data();
		create_info.width = win_width;
		create_info.height = win_height;
		create_info.layers = 1;
		VkFramebuffer fb = VK_NULL_HANDLE;
		CHECK_VULKAN(vkCreateFramebuffer(vk_device, &create_info, nullptr, &fb));
		framebuffers.push_back(fb);
	}

	// Setup the command pool
	VkCommandPool vk_command_pool;
	{
		VkCommandPoolCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		create_info.queueFamilyIndex = graphics_queue_index;
		CHECK_VULKAN(vkCreateCommandPool(vk_device, &create_info, nullptr, &vk_command_pool));
	}

	std::vector<VkCommandBuffer> command_buffers(framebuffers.size(), VkCommandBuffer{});
	{
		VkCommandBufferAllocateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		info.commandPool = vk_command_pool;
		info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		info.commandBufferCount = command_buffers.size();
		CHECK_VULKAN(vkAllocateCommandBuffers(vk_device, &info, command_buffers.data()));
	}

	// Upload vertex data to the GPU by staging in host memory, then copying to GPU memory
	VkBuffer vertex_buffer = VK_NULL_HANDLE;
	VkDeviceMemory vertex_mem = VK_NULL_HANDLE;
	{
		VkBufferCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		info.size = sizeof(float) * triangle_verts.size();
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkBuffer upload_buffer = VK_NULL_HANDLE;
		info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		CHECK_VULKAN(vkCreateBuffer(vk_device, &info, nullptr, &upload_buffer));
		
		info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		CHECK_VULKAN(vkCreateBuffer(vk_device, &info, nullptr, &vertex_buffer));

		VkMemoryRequirements mem_reqs = {};
		vkGetBufferMemoryRequirements(vk_device, vertex_buffer, &mem_reqs);

		// Allocate the upload staging buffer
		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = mem_reqs.size;
		alloc_info.memoryTypeIndex = get_memory_type_index(mem_reqs.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, mem_props);
		VkDeviceMemory upload_mem = VK_NULL_HANDLE;
		CHECK_VULKAN(vkAllocateMemory(vk_device, &alloc_info, nullptr, &upload_mem));
		vkBindBufferMemory(vk_device, upload_buffer, upload_mem, 0);

		alloc_info.memoryTypeIndex = get_memory_type_index(mem_reqs.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mem_props);
		CHECK_VULKAN(vkAllocateMemory(vk_device, &alloc_info, nullptr, &vertex_mem));
		vkBindBufferMemory(vk_device, vertex_buffer, vertex_mem, 0);

		// Now map the upload heap buffer and copy our data in
		float *upload_mapping = nullptr;
		vkMapMemory(vk_device, upload_mem, 0, info.size, 0, reinterpret_cast<void**>(&upload_mapping));
		std::memcpy(upload_mapping, triangle_verts.data(), info.size);
		vkUnmapMemory(vk_device, upload_mem);

		// Grab one of our command buffers and use it to record and run the upload
		auto& cmd_buf = command_buffers[0];

		VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		CHECK_VULKAN(vkBeginCommandBuffer(cmd_buf, &begin_info));

		VkBufferCopy copy_cmd = {};
		copy_cmd.size = info.size;
		vkCmdCopyBuffer(cmd_buf, upload_buffer, vertex_buffer, 1, &copy_cmd);

		CHECK_VULKAN(vkEndCommandBuffer(cmd_buf));

		// Submit the copy to run
		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffers[0];
		CHECK_VULKAN(vkQueueSubmit(vk_queue, 1, &submit_info, VK_NULL_HANDLE));
		vkQueueWaitIdle(vk_queue);

		// We didn't make the buffers individually reset-able, but we're just using it as temp
		// one to do this upload so clear the pool to reset
		vkResetCommandPool(vk_device, vk_command_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

		vkDestroyBuffer(vk_device, upload_buffer, nullptr);
		vkFreeMemory(vk_device, upload_mem, nullptr);
	}

	VkBuffer index_buffer = VK_NULL_HANDLE;
	VkDeviceMemory index_mem = VK_NULL_HANDLE;
	{
		VkBufferCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		info.size = sizeof(uint32_t) * triangle_indices.size();
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkBuffer upload_buffer = VK_NULL_HANDLE;
		info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		CHECK_VULKAN(vkCreateBuffer(vk_device, &info, nullptr, &upload_buffer));

		info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		CHECK_VULKAN(vkCreateBuffer(vk_device, &info, nullptr, &index_buffer));

		VkMemoryRequirements mem_reqs = {};
		vkGetBufferMemoryRequirements(vk_device, index_buffer, &mem_reqs);

		// Allocate the upload staging buffer
		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = mem_reqs.size;
		alloc_info.memoryTypeIndex = get_memory_type_index(mem_reqs.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, mem_props);
		VkDeviceMemory upload_mem = VK_NULL_HANDLE;
		CHECK_VULKAN(vkAllocateMemory(vk_device, &alloc_info, nullptr, &upload_mem));
		vkBindBufferMemory(vk_device, upload_buffer, upload_mem, 0);

		alloc_info.memoryTypeIndex = get_memory_type_index(mem_reqs.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mem_props);
		CHECK_VULKAN(vkAllocateMemory(vk_device, &alloc_info, nullptr, &index_mem));
		vkBindBufferMemory(vk_device, index_buffer, index_mem, 0);

		// Now map the upload heap buffer and copy our data in
		uint32_t* upload_mapping = nullptr;
		vkMapMemory(vk_device, upload_mem, 0, info.size, 0, reinterpret_cast<void**>(&upload_mapping));
		std::memcpy(upload_mapping, triangle_indices.data(), info.size);
		vkUnmapMemory(vk_device, upload_mem);

		// Grab one of our command buffers and use it to record and run the upload
		auto& cmd_buf = command_buffers[0];

		VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		CHECK_VULKAN(vkBeginCommandBuffer(cmd_buf, &begin_info));

		VkBufferCopy copy_cmd = {};
		copy_cmd.size = info.size;
		vkCmdCopyBuffer(cmd_buf, upload_buffer, index_buffer, 1, &copy_cmd);

		CHECK_VULKAN(vkEndCommandBuffer(cmd_buf));

		// Submit the copy to run
		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffers[0];
		CHECK_VULKAN(vkQueueSubmit(vk_queue, 1, &submit_info, VK_NULL_HANDLE));
		vkQueueWaitIdle(vk_queue);

		// We didn't make the buffers individually reset-able, but we're just using it as temp
		// one to do this upload so clear the pool to reset
		vkResetCommandPool(vk_device, vk_command_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

		vkDestroyBuffer(vk_device, upload_buffer, nullptr);
		vkFreeMemory(vk_device, upload_mem, nullptr);
	}

	// Build the bottom level acceleration structure
	VkAccelerationStructureNV blas = VK_NULL_HANDLE;
	VkDeviceMemory blas_mem = VK_NULL_HANDLE;
	uint64_t blas_handle = 0;
	{
		VkGeometryNV geom_desc = {};
		geom_desc.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV;
		geom_desc.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_NV;
		geom_desc.geometry.triangles.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV;
		geom_desc.geometry.triangles.vertexData = vertex_buffer;
		geom_desc.geometry.triangles.vertexOffset = 0;
		geom_desc.geometry.triangles.vertexCount = triangle_verts.size() / 3;
		geom_desc.geometry.triangles.vertexStride = 3 * sizeof(float);
		geom_desc.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		geom_desc.geometry.triangles.indexData = index_buffer;
		geom_desc.geometry.triangles.indexOffset = 0;
		geom_desc.geometry.triangles.indexCount = triangle_indices.size();
		geom_desc.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
		geom_desc.geometry.triangles.transformData = VK_NULL_HANDLE;
		geom_desc.geometry.triangles.transformOffset = 0;
		geom_desc.flags = VK_GEOMETRY_OPAQUE_BIT_NV;
		// Must be set even if not used
		geom_desc.geometry.aabbs = {};
		geom_desc.geometry.aabbs.sType = { VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV };

		VkAccelerationStructureInfoNV accel_info = {};
		accel_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
		accel_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
		accel_info.instanceCount = 0;
		accel_info.geometryCount = 1;
		accel_info.pGeometries = &geom_desc;

		VkAccelerationStructureCreateInfoNV create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
		create_info.info = accel_info;
		CHECK_VULKAN(vkCreateAccelerationStructure(vk_device, &create_info, nullptr, &blas));

		// Determine how much memory the acceleration structure will need
		VkAccelerationStructureMemoryRequirementsInfoNV mem_info = {};
		mem_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
		mem_info.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
		mem_info.accelerationStructure = blas;
		
		VkMemoryRequirements2 mem_reqs = {};
		vkGetAccelerationStructureMemoryRequirements(vk_device, &mem_info, &mem_reqs);
		// TODO WILL: For a single triangle it requests 64k output and 64k scratch? It seems like a lot.
		std::cout << "BLAS will need " << mem_reqs.memoryRequirements.size << "b output space\n";

		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = mem_reqs.memoryRequirements.size;
		alloc_info.memoryTypeIndex = get_memory_type_index(mem_reqs.memoryRequirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mem_props);
		CHECK_VULKAN(vkAllocateMemory(vk_device, &alloc_info, nullptr, &blas_mem));

		// Determine how much additional memory we need for the build
		mem_info.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
		vkGetAccelerationStructureMemoryRequirements(vk_device, &mem_info, &mem_reqs);
		std::cout << "BLAS will need " << mem_reqs.memoryRequirements.size << "b scratch space\n";

		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = mem_reqs.memoryRequirements.size;
		alloc_info.memoryTypeIndex = get_memory_type_index(mem_reqs.memoryRequirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mem_props);
		VkDeviceMemory scratch_mem = VK_NULL_HANDLE;
		CHECK_VULKAN(vkAllocateMemory(vk_device, &alloc_info, nullptr, &scratch_mem));
		
		// Associate the scratch mem with a buffer
		VkBufferCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		info.size = mem_reqs.memoryRequirements.size;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkBuffer scratch_buffer = VK_NULL_HANDLE;
		info.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
		CHECK_VULKAN(vkCreateBuffer(vk_device, &info, nullptr, &scratch_buffer));
		vkBindBufferMemory(vk_device, scratch_buffer, scratch_mem, 0);

		VkBindAccelerationStructureMemoryInfoNV bind_mem_info = {};
		bind_mem_info.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
		bind_mem_info.accelerationStructure = blas;
		bind_mem_info.memory = blas_mem;
		CHECK_VULKAN(vkBindAccelerationStructureMemory(vk_device, 1, &bind_mem_info));
		
		// Grab one of our command buffers and use it to record and run the upload
		auto &cmd_buf = command_buffers[0];

		VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		CHECK_VULKAN(vkBeginCommandBuffer(cmd_buf, &begin_info));

		vkCmdBuildAccelerationStructure(cmd_buf, &accel_info, VK_NULL_HANDLE, 0, false, blas, VK_NULL_HANDLE, scratch_buffer, 0);

		CHECK_VULKAN(vkEndCommandBuffer(cmd_buf));

		// Submit the copy to run
		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffers[0];
		CHECK_VULKAN(vkQueueSubmit(vk_queue, 1, &submit_info, VK_NULL_HANDLE));
		vkQueueWaitIdle(vk_queue);

		CHECK_VULKAN(vkGetAccelerationStructureHandle(vk_device, blas, sizeof(uint64_t), &blas_handle));

		// We didn't make the buffers individually reset-able, but we're just using it as temp
		// one to do this upload so clear the pool to reset
		vkResetCommandPool(vk_device, vk_command_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

		// TODO LATER compaction via vkCmdCopyAccelerationStructureNV

		vkDestroyBuffer(vk_device, scratch_buffer, nullptr);
		vkFreeMemory(vk_device, scratch_mem, nullptr);
	}

	// Write the instance data
	VkBuffer instance_buffer = VK_NULL_HANDLE;
	VkDeviceMemory instance_mem = VK_NULL_HANDLE;
	{
		VkBufferCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		info.size = sizeof(VkGeometryInstanceNV);
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkBuffer upload_buffer = VK_NULL_HANDLE;
		info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		CHECK_VULKAN(vkCreateBuffer(vk_device, &info, nullptr, &upload_buffer));

		info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		CHECK_VULKAN(vkCreateBuffer(vk_device, &info, nullptr, &instance_buffer));

		VkMemoryRequirements mem_reqs = {};
		vkGetBufferMemoryRequirements(vk_device, instance_buffer, &mem_reqs);

		// Allocate the upload staging buffer
		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = mem_reqs.size;
		alloc_info.memoryTypeIndex = get_memory_type_index(mem_reqs.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, mem_props);
		VkDeviceMemory upload_mem = VK_NULL_HANDLE;
		CHECK_VULKAN(vkAllocateMemory(vk_device, &alloc_info, nullptr, &upload_mem));
		vkBindBufferMemory(vk_device, upload_buffer, upload_mem, 0);

		alloc_info.memoryTypeIndex = get_memory_type_index(mem_reqs.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mem_props);
		CHECK_VULKAN(vkAllocateMemory(vk_device, &alloc_info, nullptr, &instance_mem));
		vkBindBufferMemory(vk_device, instance_buffer, instance_mem, 0);

		// Now map the upload heap buffer and copy our data in
		VkGeometryInstanceNV *upload_mapping = nullptr;
		vkMapMemory(vk_device, upload_mem, 0, info.size, 0, reinterpret_cast<void**>(&upload_mapping));
		std::memset(upload_mapping, 0, sizeof(VkGeometryInstanceNV));

		// Transform is 4x3 row-major
		upload_mapping->transform[0] = 1.f;
		upload_mapping->transform[4] = 1.f;
		upload_mapping->transform[8] = 1.f;
		upload_mapping->transform[11] = 1.f;
		upload_mapping->instance_custom_index = 0;
		upload_mapping->mask = 0xff;
		upload_mapping->instance_offset = 0;
		upload_mapping->acceleration_structure_handle = blas_handle;

		vkUnmapMemory(vk_device, upload_mem);

		// Grab one of our command buffers and use it to record and run the upload
		auto& cmd_buf = command_buffers[0];

		VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		CHECK_VULKAN(vkBeginCommandBuffer(cmd_buf, &begin_info));

		VkBufferCopy copy_cmd = {};
		copy_cmd.size = info.size;
		vkCmdCopyBuffer(cmd_buf, upload_buffer, instance_buffer, 1, &copy_cmd);

		CHECK_VULKAN(vkEndCommandBuffer(cmd_buf));

		// Submit the copy to run
		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffers[0];
		CHECK_VULKAN(vkQueueSubmit(vk_queue, 1, &submit_info, VK_NULL_HANDLE));
		vkQueueWaitIdle(vk_queue);

		// We didn't make the buffers individually reset-able, but we're just using it as temp
		// one to do this upload so clear the pool to reset
		vkResetCommandPool(vk_device, vk_command_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

		vkDestroyBuffer(vk_device, upload_buffer, nullptr);
		vkFreeMemory(vk_device, upload_mem, nullptr);
	}

	// Build the top level acceleration structure
	VkAccelerationStructureNV tlas = VK_NULL_HANDLE;
	VkDeviceMemory tlas_mem = VK_NULL_HANDLE;
	uint64_t tlas_handle = 0;
	{
		VkAccelerationStructureInfoNV accel_info = {};
		accel_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
		accel_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
		accel_info.instanceCount = 1;
		accel_info.geometryCount = 0;

		VkAccelerationStructureCreateInfoNV create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
		create_info.info = accel_info;
		CHECK_VULKAN(vkCreateAccelerationStructure(vk_device, &create_info, nullptr, &tlas));

		// Determine how much memory the acceleration structure will need
		VkAccelerationStructureMemoryRequirementsInfoNV mem_info = {};
		mem_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
		mem_info.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
		mem_info.accelerationStructure = tlas;

		VkMemoryRequirements2 mem_reqs = {};
		vkGetAccelerationStructureMemoryRequirements(vk_device, &mem_info, &mem_reqs);
		// TODO WILL: For a single triangle it requests 64k output and 64k scratch? It seems like a lot.
		std::cout << "TLAS will need " << mem_reqs.memoryRequirements.size << "b output space\n";

		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = mem_reqs.memoryRequirements.size;
		alloc_info.memoryTypeIndex = get_memory_type_index(mem_reqs.memoryRequirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mem_props);
		CHECK_VULKAN(vkAllocateMemory(vk_device, &alloc_info, nullptr, &tlas_mem));

		// Determine how much additional memory we need for the build
		mem_info.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
		vkGetAccelerationStructureMemoryRequirements(vk_device, &mem_info, &mem_reqs);
		std::cout << "TLAS will need " << mem_reqs.memoryRequirements.size << "b scratch space\n";

		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = mem_reqs.memoryRequirements.size;
		alloc_info.memoryTypeIndex = get_memory_type_index(mem_reqs.memoryRequirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mem_props);
		VkDeviceMemory scratch_mem = VK_NULL_HANDLE;
		CHECK_VULKAN(vkAllocateMemory(vk_device, &alloc_info, nullptr, &scratch_mem));

		// Associate the scratch mem with a buffer
		VkBufferCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		info.size = mem_reqs.memoryRequirements.size;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkBuffer scratch_buffer = VK_NULL_HANDLE;
		info.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
		CHECK_VULKAN(vkCreateBuffer(vk_device, &info, nullptr, &scratch_buffer));
		vkBindBufferMemory(vk_device, scratch_buffer, scratch_mem, 0);

		VkBindAccelerationStructureMemoryInfoNV bind_mem_info = {};
		bind_mem_info.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
		bind_mem_info.accelerationStructure = tlas;
		bind_mem_info.memory = tlas_mem;
		CHECK_VULKAN(vkBindAccelerationStructureMemory(vk_device, 1, &bind_mem_info));

		// Grab one of our command buffers and use it to record and run the upload
		auto& cmd_buf = command_buffers[0];

		VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		CHECK_VULKAN(vkBeginCommandBuffer(cmd_buf, &begin_info));

		vkCmdBuildAccelerationStructure(cmd_buf, &accel_info, instance_buffer, 0, false, tlas, VK_NULL_HANDLE, scratch_buffer, 0);

		CHECK_VULKAN(vkEndCommandBuffer(cmd_buf));

		// Submit the copy to run
		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffers[0];
		CHECK_VULKAN(vkQueueSubmit(vk_queue, 1, &submit_info, VK_NULL_HANDLE));
		vkQueueWaitIdle(vk_queue);

		CHECK_VULKAN(vkGetAccelerationStructureHandle(vk_device, tlas, sizeof(uint64_t), &tlas_handle));

		// We didn't make the buffers individually reset-able, but we're just using it as temp
		// one to do this upload so clear the pool to reset
		vkResetCommandPool(vk_device, vk_command_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

		// TODO LATER compaction via vkCmdCopyAccelerationStructureNV

		vkDestroyBuffer(vk_device, scratch_buffer, nullptr);
		vkFreeMemory(vk_device, scratch_mem, nullptr);
	}

	// Now record the rendering commands (TODO: Could also do this pre-recording in the DXR backend
	// of rtobj. Will there be much perf. difference?)
	for (size_t i = 0; i < command_buffers.size(); ++i) {
		auto& cmd_buf = command_buffers[i];

		VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		CHECK_VULKAN(vkBeginCommandBuffer(cmd_buf, &begin_info));

		VkRenderPassBeginInfo render_pass_info = {};
		render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		render_pass_info.renderPass = vk_render_pass;
		render_pass_info.framebuffer = framebuffers[i];
		render_pass_info.renderArea.offset.x = 0;
		render_pass_info.renderArea.offset.y = 0;
		render_pass_info.renderArea.extent = swapchain_extent;
		
		VkClearValue clear_color = { 0.f, 0.f, 0.f, 1.f };
		render_pass_info.clearValueCount = 1;
		render_pass_info.pClearValues = &clear_color;

		vkCmdBeginRenderPass(cmd_buf, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);

		// Draw our "triangle" embedded in the shader
		vkCmdDraw(cmd_buf, 3, 1, 0, 0);

		vkCmdEndRenderPass(cmd_buf);

		CHECK_VULKAN(vkEndCommandBuffer(cmd_buf));
	}

	VkSemaphore img_avail_semaphore = VK_NULL_HANDLE;
	VkSemaphore render_finished_semaphore = VK_NULL_HANDLE;
	{
		VkSemaphoreCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		CHECK_VULKAN(vkCreateSemaphore(vk_device, &info, nullptr, &img_avail_semaphore));
		CHECK_VULKAN(vkCreateSemaphore(vk_device, &info, nullptr, &render_finished_semaphore));
	}

	// We use a fence to wait for the rendering work to finish
	VkFence vk_fence = VK_NULL_HANDLE;
	{
		VkFenceCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		CHECK_VULKAN(vkCreateFence(vk_device, &info, nullptr, &vk_fence));
	}

	std::cout << "Running loop\n";
	bool done = false;
	while (!done) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				done = true;
			}
			if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
				done = true;
			}
			if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE
					&& event.window.windowID == SDL_GetWindowID(window)) {
				done = true;
			}
		}

		// Get an image from the swap chain
		uint32_t img_index = 0;
		CHECK_VULKAN(vkAcquireNextImageKHR(vk_device, vk_swapchain, std::numeric_limits<uint64_t>::max(),
			img_avail_semaphore, VK_NULL_HANDLE, &img_index));

		// We need to wait for the image before we can run the commands to draw to it, and signal
		// the render finished one when we're done
		const std::array<VkSemaphore, 1> wait_semaphores = { img_avail_semaphore };
		const std::array<VkSemaphore, 1> signal_semaphores = { render_finished_semaphore };
		const std::array<VkPipelineStageFlags, 1> wait_stages = { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };

		CHECK_VULKAN(vkResetFences(vk_device, 1, &vk_fence));
		
		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.waitSemaphoreCount = wait_semaphores.size();
		submit_info.pWaitSemaphores = wait_semaphores.data();
		submit_info.pWaitDstStageMask = wait_stages.data();
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffers[img_index];
		submit_info.signalSemaphoreCount = signal_semaphores.size();
		submit_info.pSignalSemaphores = signal_semaphores.data();
		CHECK_VULKAN(vkQueueSubmit(vk_queue, 1, &submit_info, vk_fence));

		// Finally, present the updated image in the swap chain
		std::array<VkSwapchainKHR, 1> present_chain = { vk_swapchain };
		VkPresentInfoKHR present_info = {};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.waitSemaphoreCount = signal_semaphores.size();
		present_info.pWaitSemaphores = signal_semaphores.data();
		present_info.swapchainCount = present_chain.size();
		present_info.pSwapchains = present_chain.data();
		present_info.pImageIndices = &img_index;
		CHECK_VULKAN(vkQueuePresentKHR(vk_queue, &present_info));

		// Wait for the frame to finish
		CHECK_VULKAN(vkWaitForFences(vk_device, 1, &vk_fence, true, std::numeric_limits<uint64_t>::max()));
	}

	vkDestroySemaphore(vk_device, img_avail_semaphore, nullptr);
	vkDestroySemaphore(vk_device, render_finished_semaphore, nullptr);
	vkDestroyFence(vk_device, vk_fence, nullptr);
	vkDestroyCommandPool(vk_device, vk_command_pool, nullptr);
	vkDestroySwapchainKHR(vk_device, vk_swapchain, nullptr);
	for (auto &fb : framebuffers) {
		vkDestroyFramebuffer(vk_device, fb, nullptr);
	}
	vkDestroyPipeline(vk_device, vk_pipeline, nullptr);
	vkDestroyRenderPass(vk_device, vk_render_pass, nullptr);
	vkDestroyPipelineLayout(vk_device, vk_pipeline_layout, nullptr);
	for (auto &v : swapchain_image_views) {
		vkDestroyImageView(vk_device, v, nullptr);
	}	
	vkDestroySurfaceKHR(vk_instance, vk_surface, nullptr);
	vkDestroyDevice(vk_device, nullptr);
	vkDestroyInstance(vk_instance, nullptr);

	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}

