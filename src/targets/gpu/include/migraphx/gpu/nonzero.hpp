#ifndef MIGRAPHX_GUARD_RTGLIB_NONZERO_HPP
#define MIGRAPHX_GUARD_RTGLIB_NONZERO_HPP

#include <migraphx/argument.hpp>
#include <migraphx/reflect.hpp>
#include <migraphx/op/nonzero.hpp>
#include <migraphx/gpu/miopen.hpp>

namespace migraphx {
inline namespace MIGRAPHX_INLINE_NS {
namespace gpu {

struct context;

struct hip_nonzero
{
    op::nonzero op;

    template <class Self, class F>
    static auto reflect(Self& self, F f)
    {
        return migraphx::reflect(self.op, f);
    }

    std::string name() const { return "gpu::nonzero"; }
    shape compute_shape(std::vector<shape> inputs) const;
    argument
    compute(context& ctx, const shape& output_shape, const std::vector<argument>& args) const;
    std::ptrdiff_t output_alias(const std::vector<shape>& shapes) const
    {
        return shapes.size() - 1;
    }
};

} // namespace gpu
} // namespace MIGRAPHX_INLINE_NS
} // namespace migraphx

#endif