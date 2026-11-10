package("directxmath-macos")
    set_base("directxmath")
    on_install("macosx", function (package)
        -- Apple libc++ does not introduce size_t into the global namespace through
        -- incidental standard-library includes. DirectXMath uses it unqualified.
        io.replace("Inc/DirectXMath.h", "#pragma once", "#pragma once\n#include <stddef.h>", {plain = true})
        os.cp("../resources/headers/sal.h", package:installdir("include", "directxmath"))
        import("package.tools.cmake").install(package, {"-DBUILD_TESTING=OFF"})
    end)
package_end()
