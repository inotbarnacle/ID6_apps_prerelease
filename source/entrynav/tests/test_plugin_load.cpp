// Loads a plugin the way HmiMain3D's ManagedPluginApp::init does, with the car's own HMI libraries under QEMU:
// libAppFramework global, the plugin with RTLD_NOW (every symbol it needs must resolve), then dlsym "pluginAppInfo"
// with create at +0x10 and destroy at +0x14. create() is not called: it needs HmiMain3D's dispatcher.
//   test-plugin-load PLUGIN.so NAME
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                    \
    do {                                                            \
        if (!(x)) {                                                 \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);    \
            return 1;                                               \
        }                                                           \
    } while (0)

int main(int argc, char** argv) {
    CHECK(argc == 3);
    void* framework = dlopen("libAppFramework.A.so", RTLD_NOW | RTLD_GLOBAL);  // linked into HmiMain3D: global
    if (!framework) fprintf(stderr, "%s\n", dlerror());
    CHECK(framework);
    void* plugin = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!plugin) fprintf(stderr, "%s\n", dlerror());
    CHECK(plugin);
    const unsigned char* info = static_cast<const unsigned char*>(dlsym(plugin, "pluginAppInfo"));
    CHECK(info);
    uint16_t major, minor;
    uint32_t rhmi;
    const char* name;
    void* create;
    void* destroy;
    memcpy(&major, info, 2);
    memcpy(&minor, info + 2, 2);
    memcpy(&name, info + 4, 4);
    memcpy(&rhmi, info + 0xc, 4);
    memcpy(&create, info + 0x10, 4);
    memcpy(&destroy, info + 0x14, 4);
    CHECK(sizeof(void*) == 4);
    CHECK(major == 1 && minor == 0 && rhmi == 1);
    CHECK(!strcmp(name, argv[2]));
    CHECK(create && destroy);
    // The descriptor is the only symbol a plugin should export to HmiMain3D.
    CHECK(!dlsym(plugin, "bd_parse") && !dlsym(plugin, "_Z10create_app"));
    printf("PASS %s loads with the car's HMI libraries; pluginAppInfo %s, create and destroy set\n", argv[1], name);
    return 0;
}
