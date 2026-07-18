#pragma once
// withAutoreleasePool — corre `body` dentro de un @autoreleasepool. Los hilos std::thread NO tienen pool; los
// caminos Metal (prepare/uploadImage/renderOffscreen del export y de los thumbnails) autoreleasean objetos
// (NSString del shader, texture descriptors, NSError) → sin pool se filtran + spamean stderr. Este shim vive
// en un .mm (PluginEditor.cpp es C++ puro y no puede usar @autoreleasepool). Drena al salir del scope.
#include <functional>

namespace supernova { void withAutoreleasePool (const std::function<void()>& body); }
