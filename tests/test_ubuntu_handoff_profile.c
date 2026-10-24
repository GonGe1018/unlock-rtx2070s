#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ubuntu_handoff_profile.h"

static uint8_t *load_exact(const char *path, size_t bytes)
{
    uint8_t *data = malloc(bytes);
    FILE *file;

    assert(data != NULL);
    file = fopen(path, "rb");
    assert(file != NULL);
    assert(fread(data, 1, bytes, file) == bytes);
    assert(fgetc(file) == EOF);
    assert(fclose(file) == 0);
    return data;
}

int main(void)
{
    uint8_t *grub = load_exact("/boot/efi/EFI/ubuntu/grubx64.efi",
                               UBUNTU_GRUB_BYTES);
    uint8_t *config = load_exact("/boot/efi/EFI/ubuntu/grub.cfg",
                                 UBUNTU_GRUB_CFG_BYTES);
    unsigned cases = 0;

    assert(ubuntu_handoff_grub_matches(grub, UBUNTU_GRUB_BYTES));
    ++cases;
    grub[0] ^= 1;
    assert(!ubuntu_handoff_grub_matches(grub, UBUNTU_GRUB_BYTES));
    grub[0] ^= 1;
    ++cases;
    grub[UBUNTU_GRUB_BYTES - 1] ^= 1;
    assert(!ubuntu_handoff_grub_matches(grub, UBUNTU_GRUB_BYTES));
    grub[UBUNTU_GRUB_BYTES - 1] ^= 1;
    ++cases;
    assert(!ubuntu_handoff_grub_matches(grub, UBUNTU_GRUB_BYTES - 1));
    ++cases;

    assert(ubuntu_handoff_config_matches(config, UBUNTU_GRUB_CFG_BYTES));
    ++cases;
    config[0] ^= 1;
    assert(!ubuntu_handoff_config_matches(config, UBUNTU_GRUB_CFG_BYTES));
    config[0] ^= 1;
    ++cases;
    assert(!ubuntu_handoff_config_matches(
        config, UBUNTU_GRUB_CFG_BYTES - 1));
    ++cases;

    free(config);
    free(grub);
    printf("PASS: %u exact Ubuntu same-boot handoff profile checks\n",
           cases);
    return 0;
}
