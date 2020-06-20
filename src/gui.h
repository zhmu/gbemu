#pragma once

#include <array>

namespace gb {
class IO;
namespace gui {

void InitGL();
void Init();
void Cleanup();
void Render();
void UpdateTexture(const char*);
bool HandleEvents(IO&);

void OnAudioSample(const int ch, const float sample);

}
}