#pragma once

namespace esteria::loopbackcheck {
// Actual client networking against an owned TCP peer; never calls CommonMain.
int Run();
}
