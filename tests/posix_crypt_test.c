#include <crypt.h>
#include <assert.h>
#include <string.h>

int main(void)
{
    static const char hash[] =
        "$5$facet$j7FgoXidvJl10CTaW0nguGP3ZnvKnqS3/IHmDVliPQ9";
    assert(strcmp(crypt("facetos", hash), hash) == 0);
    assert(strcmp(crypt("incorrect", hash), hash) != 0);
    assert(strcmp(crypt("facetos", "$6$unsupported"), "*") == 0);
    return 0;
}
