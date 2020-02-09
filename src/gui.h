#include <SDL.h>

namespace gb::gui {

void ProcessEvent(SDL_Event* event);
void InitGL();
void Init();
void Cleanup();
void Render();

}
