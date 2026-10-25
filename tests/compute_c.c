#include <vultra/api/vultra_compute.generated.h>

#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(VultraStatus) == 4, "ABI status must remain 32 bits with short enums");
#define REQUIRE(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            fprintf(stderr, "Compute C failure at line %d\n", __LINE__); \
            result = 1; \
            goto cleanup; \
        } \
    } while (0)

int testComputeC(const VultraComputeApi* api)
{
    int                   result  = 0;
    VultraComputeContext* context = NULL;
    VultraComputeContext* other   = NULL;
    char                  error[2048];
    float                 input[131];
    float                 output[131];
    const char*           path        = "tests/shaders/compute.slang";
    uint64_t              first       = 0;
    uint64_t              second      = 0;
    uint64_t              destination = 0;
    uint64_t              kernel      = 0;
    uint64_t              count       = 0;

    struct Constants
    {
        uint32_t count;
        float    gain;
    } constants = {131, 0.5f};

    REQUIRE(api->version == 1 && api->struct_size == sizeof(*api));
    REQUIRE(api->fingerprint == VULTRA_COMPUTE_API_FINGERPRINT);
    REQUIRE(api->create_context(1, &context, error, sizeof(error)) == VULTRA_STATUS_OK);
    REQUIRE(api->create_context(1, &other, error, sizeof(error)) == VULTRA_STATUS_OK);
    REQUIRE(api->create_buffer(context, 131, "float", 5, &first) == VULTRA_STATUS_OK);
    REQUIRE(api->create_buffer(context, 131, "float", 5, &second) == VULTRA_STATUS_OK);
    REQUIRE(api->create_buffer(context, 131, "float", 5, &destination) == VULTRA_STATUS_OK);
    REQUIRE(api->read_buffer(context, first, output, sizeof(output)) == VULTRA_STATUS_INVALID_ARGUMENT);
    REQUIRE(api->read_buffer(other, first, output, sizeof(output)) == VULTRA_STATUS_INVALID_ARGUMENT);
    REQUIRE(api->write_buffer(context, first, NULL, sizeof(input)) == VULTRA_STATUS_INVALID_ARGUMENT);
    REQUIRE(api->write_buffer(context, first, input, sizeof(input) - 1) == VULTRA_STATUS_INVALID_ARGUMENT);
    for (uint32_t index = 0; index < 131; ++index)
    {
        input[index] = (float)index;
    }
    REQUIRE(api->write_buffer(context, first, input, sizeof(input)) == VULTRA_STATUS_OK);
    REQUIRE(api->write_buffer(context, second, input, sizeof(input)) == VULTRA_STATUS_OK);
    REQUIRE(api->load_kernel(context, path, strlen(path), "main", 4, &kernel) == VULTRA_STATUS_OK);
    REQUIRE(api->kernel_layout(context, kernel, NULL, 0, &count) == VULTRA_STATUS_OK && count > 0);
    REQUIRE(api->kernel_layout(context, kernel, error, 1, &count) == VULTRA_STATUS_BUFFER_TOO_SMALL);
    {
        uint64_t ids[3] = {first, second, destination};
        REQUIRE(api->dispatch(context, kernel, ids, 3, &constants, sizeof(constants), 3, 1, 1) == VULTRA_STATUS_OK);
        REQUIRE(api->read_buffer(context, destination, output, sizeof(output)) == VULTRA_STATUS_OK);
        for (uint32_t index = 0; index < 131; ++index)
        {
            REQUIRE(output[index] == (float)index);
        }
        REQUIRE(api->dispatch(context, kernel, ids, 3, &constants, sizeof(constants) - 1, 3, 1, 1) ==
                VULTRA_STATUS_INVALID_ARGUMENT);
        REQUIRE(api->dispatch(context, kernel, ids, 3, &constants, sizeof(constants), 0, 1, 1) ==
                VULTRA_STATUS_INVALID_ARGUMENT);
        REQUIRE(api->dispatch(context, first, ids, 3, &constants, sizeof(constants), 3, 1, 1) ==
                VULTRA_STATUS_INVALID_ARGUMENT);
    }
    REQUIRE(api->release_buffer(context, first) == VULTRA_STATUS_OK);
    REQUIRE(api->release_buffer(context, first) == VULTRA_STATUS_INVALID_ARGUMENT);
    REQUIRE(api->last_error(context, NULL, 0, &count) == VULTRA_STATUS_OK && count > 0);
    REQUIRE(api->last_error(context, error, sizeof(error), &count) == VULTRA_STATUS_OK);
    REQUIRE(count < sizeof(error));
    error[count] = 0;
    REQUIRE(strstr(error, "foreign, released or has the wrong type") != NULL);
    REQUIRE(api->release_kernel(context, kernel) == VULTRA_STATUS_OK);
    REQUIRE(api->kernel_layout(context, kernel, NULL, 0, &count) == VULTRA_STATUS_INVALID_ARGUMENT);
    puts("Compute C ABI passed: GPU values, foreign/stale/type checks, byte bounds, diagnostics and ownership");
cleanup:
    api->destroy_context(other);
    api->destroy_context(context);
    return result;
}
