#pragma once

#if defined(__has_include)
#  if __has_include("esp_vfs_spiffs.h")
#    include "esp_vfs_spiffs.h"
#    define SPIFFS_AVAILABLE 1
#  elif __has_include("esp_spiffs.h")
#    include "esp_spiffs.h"
#    define SPIFFS_AVAILABLE 1
#  else
#    error "SPIFFS header not found: install the SPIFFS component or adjust include paths"
#  endif
#else
#  include "esp_vfs_spiffs.h" // fallback; may still fail if header missing
#  define SPIFFS_AVAILABLE 1
#endif
