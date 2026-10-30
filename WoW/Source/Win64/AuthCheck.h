#pragma once

namespace esteria::authcheck {
// Actual Grunt/SRP callbacks against an explicitly owned local auth fixture.
// Never initializes CommonMain or loads game assets.
int Run();
}
