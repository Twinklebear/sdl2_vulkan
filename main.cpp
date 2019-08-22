#include <iostream>
#include <vector>
#include <string>
#include <array>
#include <SDL.h>
#include <SDL_syswm.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#define CHECK_VULKAN(FN) \
	{ \
		VkResult r = FN; \
		if (r != VK_SUCCESS) {\
			std::cout << #FN << " failed\n" << std::flush; \
			throw std::runtime_error(#FN " failed!");  \
		} \
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

	// Make the Vulkan Instance
	VkInstance vk_instance = VK_NULL_HANDLE;
	{
		VkApplicationInfo app_info = {};
		app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app_info.pApplicationName = "SDL2 + Vulkan";
		app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		app_info.pEngineName = "None";
		app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		app_info.apiVersion = VK_API_VERSION_1_0;

		std::array<const char*, 2> extension_names = {
			VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
		};
		std::array<const char*, 1> validation_layers = {
			"VK_LAYER_KHRONOS_validation"
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

	VkPhysicalDevice vk_device = VK_NULL_HANDLE;
	{
		uint32_t device_count = 0;
		vkEnumeratePhysicalDevices(vk_instance, &device_count, nullptr);
		std::cout << "Found " << device_count << " devices\n";
		std::vector<VkPhysicalDevice> devices(device_count, VkPhysicalDevice{});
		vkEnumeratePhysicalDevices(vk_instance, &device_count, devices.data());

		for (const auto &d: devices) {
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
			if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
				vk_device = d;
			}
		}
	}

	SDL_SysWMinfo wm_info;
	SDL_VERSION(&wm_info.version);
	SDL_GetWindowWMInfo(window, &wm_info);
	HWND win_handle = wm_info.info.win.window;

	VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };

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
	}

	vkDestroyInstance(vk_instance, nullptr);

	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}

