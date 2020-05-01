#ifndef MIGRAPHX_GUARD_OPERATORS_RNN_LAST_HS_OUTPUT_HPP
#define MIGRAPHX_GUARD_OPERATORS_RNN_LAST_HS_OUTPUT_HPP

#include <migraphx/op/rnn_last_output.hpp>

namespace migraphx {
inline namespace MIGRAPHX_INLINE_NS {
namespace op {

struct rnn_last_hs_output : rnn_last_output<rnn_last_hs_output>
{
    rnn_last_hs_output() {}
    rnn_last_hs_output(rnn_direction dirct) : rnn_last_output(dirct) {}

    std::string name() const { return "rnn_last_hs_output"; }
};

} // namespace op
} // namespace MIGRAPHX_INLINE_NS
} // namespace migraphx

#endif
