// Engine Bing X2d.cpp: 定义应用程序的入口点。
//

#include "main.hpp"

int main()
{
    std::string s;
    core::hidden_console(false);

    sf::RenderWindow window(sf::VideoMode(640, 480), "Echo Engine Legacy", sf::Style::Close);

    window.setFramerateLimit(60);

    sf::Event event{};
    sf::Time beforeTime;
    while (window.isOpen())
    {
        // std::this_thread::sleep_for(echo::core::global::framePerSecond.getFrames());
        // check all the window's events that were triggered since the last iteration of the loop
        while (window.pollEvent(event))
        {
            // "close requested" event: we close the window
            if (event.type == sf::Event::Closed)
                window.close();
        }
        echo::core::global::currentTime = echo::core::global::clock.getElapsedTime() - beforeTime;
        behaviors(echo::core::global::currentTime);
        beforeTime = echo::core::global::clock.getElapsedTime();

        window.clear();
        loopDraw(window);
        window.display();
    }

    return 0;
}
