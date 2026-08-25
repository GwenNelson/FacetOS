#include <facetos/sha256.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void check_vector(const char *input, const char *expected)
{
    uint8_t digest[32];
    char actual[65];
    facet_sha256((const uint8_t *)input, strlen(input), digest);
    facet_sha256_hex(digest, actual);
    assert(strcmp(actual, expected) == 0);
}

int main(void)
{
    check_vector("", "e3b0c44298fc1c149afbf4c8996fb924"
                     "27ae41e4649b934ca495991b7852b855");
    check_vector("abc", "ba7816bf8f01cfea414140de5dae2223"
                        "b00361a396177a9cb410ff61f20015ad");
    check_vector("facetos", "f490b96d6a372fd2fd1ab87bbe272a19"
                            "3567d04d23f5783862a187b201273f59");
    return 0;
}
