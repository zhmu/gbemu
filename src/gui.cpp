#include "gui.h"
#include "imgui.h"
#include "imgui-SFML.h"
#include <deque>
#include <stdexcept>
#include <vector>
#include "io.h"
#include "types.h"

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>

namespace gb {
namespace gui {
namespace {
    using KeyToButton = std::pair<sf::Keyboard::Key, int>;
    constexpr std::array keyToButtonMappings{
        KeyToButton{sf::Keyboard::Left, button::Left},
        KeyToButton{sf::Keyboard::Right, button::Right},
        KeyToButton{sf::Keyboard::Up, button::Up},
        KeyToButton{sf::Keyboard::Down, button::Down},
        KeyToButton{sf::Keyboard::A, button::A},
        KeyToButton{sf::Keyboard::Z, button::B},
        KeyToButton{sf::Keyboard::Return, button::Start},
        KeyToButton{sf::Keyboard::Tab, button::Select}
    };

    sf::Clock deltaClock;
    std::unique_ptr<sf::RenderWindow> window;
    std::unique_ptr<sf::Texture> texture;

    sf::Color backgroundColor;

    constexpr int fps = 60;
    constexpr int numberOfSamples = fps * 60;
    using ChannelSamples = std::deque<float>;
    std::array<ChannelSamples, 4> audio_samples;
}

void Init()
{
    for(auto& as: audio_samples)
        as.resize(numberOfSamples);

    window = std::make_unique<sf::RenderWindow>(sf::VideoMode(800, 600), "GBEMU");
    ImGui::SFML::Init(*window);
    //window->setFramerateLimit(60);

    texture = std::make_unique<sf::Texture>();
    texture->create(resolution::Width, resolution::Height);
}

void Cleanup()
{
    window.release();
    ImGui::SFML::Shutdown();
}

void Render()
{
    ImGui::SFML::Update(*window, deltaClock.restart());

    {
        ImGui::Begin("Video");
        const auto size = texture->getSize();
        ImGui::Image(*texture, sf::Vector2f(size.x, size.y));
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();
    }

    // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
    if (0) {
        static float f = 0.0f;
        static int counter = 0;

        ImGui::Begin("Audio!");

        for(int ch = 0; ch < static_cast<int>(audio_samples.size()); ++ch) {
            const auto& as = audio_samples[ch];
            std::vector<float> samples;
            samples.reserve(as.size());
            std::copy(as.begin(), as.end(), std::back_inserter(samples));

            char name[64];
            sprintf(name, "Channel %d", ch + 1);
            ImGui::PlotLines(name, samples.data(), samples.size(), 0, 0, -1.0f, 1.0f, ImVec2(0,80));
        }

        ImGui::End();
    }
    if (0) {
        static bool b;
        ImGui::ShowDemoWindow(&b);
    }

    window->clear(backgroundColor);
    ImGui::SFML::Render();
    window->display();
}

void UpdateTexture(const char* framebuffer)
{
    texture->update(reinterpret_cast<const sf::Uint8*>(framebuffer), resolution::Width, resolution::Height, 0, 0);
}

void OnAudioSample(const int ch, const float sample)
{
    if (ch < 0 || ch >= static_cast<int>(audio_samples.size()))
        return;
    auto& as = audio_samples[ch];
    as.pop_front();
    as.push_back(sample);
}

bool HandleEvents(IO& io)
{
    io.buttonPressed = 0;
    for (const auto [ key, button ]: keyToButtonMappings) {
        if (sf::Keyboard::isKeyPressed(key))
            io.buttonPressed |= button;
    }

    sf::Event event;
    while(window->pollEvent(event)) {
        ImGui::SFML::ProcessEvent(event);
        switch(event.type) {
            case sf::Event::Closed:
                return false;
        }
    }
   return true;
}


}
}