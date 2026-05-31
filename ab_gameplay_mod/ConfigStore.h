#pragma once

#include <string>

// ------------------------------------------------------------
// Capa baja de configuracion .cfg
// ------------------------------------------------------------
// Formato simple sin dependencias externas:
//   key=value
// Comentarios soportados:
//   # comentario
//   ; comentario
//   // comentario
//
// Esta capa solo sabe leer/escribir propiedades string. La validacion
// y el mapeo de valores del mod viven en GameplayConfig.*.

namespace ConfigStore
{
    bool Load(const char* path);
    bool Save();

    std::string GetProperty(const std::string& key, const std::string& defaultValue = "");
    bool EditProperty(const std::string& key, const std::string& value);

    const char* GetConfigPath();
}
