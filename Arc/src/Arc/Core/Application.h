#pragma once

#include "Arc/Core/Core.h"

#include "Arc/Core/Window.h"
#include "Arc/Core/LayerStack.h"
#include "Arc/Events/ApplicationEvent.h"

#include "Arc/Core/Timestep.h"

#include "Arc/ImGui/ImGuiLayer.h"

int main(int argc, char** argv);

namespace ArcEngine
{
	class Application
	{
	public:
		Application();
		virtual ~Application();
		
		void OnEvent(Event& e);

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* layer);

		inline static Application& Get() { return *s_Instance; }
		
		inline Window& GetWindow() { return *m_Window; }
	private:
		void Run();
		bool OnWindowClose(WindowCloseEvent& e);
		bool OnWindowResize(WindowResizeEvent& e);
		
		std::unique_ptr<Window> m_Window;
		ImGuiLayer* m_ImGuiLayer;
		bool m_Running = true;
		bool m_Minimized = false;
		LayerStack m_LayerStack;
		float m_LastFrameTime = 0.0f;
	private:
		static Application* s_Instance;
		friend int ::main(int argc, char** argv);
	};

	// Should be defined in CLIENT
	Application* CreateApplication();
}