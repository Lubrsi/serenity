//
// Created by lukew on 01/08/2021.
//

#include <AK/Types.h>

namespace Kernel {

struct [[gnu::packed]] ReadCapacityResult {
    u32 returned_logical_block_address;
    u32 block_length_in_bytes;
};

}
