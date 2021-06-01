#include <migraphx/gpu/driver/action.hpp>
#include <migraphx/gpu/driver/perf.hpp>
#include <migraphx/gpu/compile_pointwise.hpp>
#include <migraphx/gpu/context.hpp>

namespace migraphx {
inline namespace MIGRAPHX_INLINE_NS {
namespace gpu {
namespace driver {

struct compile_pointwise : action<compile_pointwise>
{
    static void apply(const parser& p, const value& v)
    {
        context ctx;
        auto inputs = p.parse_shapes(v.at("inputs"));
        auto op     = gpu::compile_pointwise(ctx, inputs, v.at("lambda").to<std::string>());
        double t    = time_op(ctx, op, inputs);
        std::cout << op << ": " << t << "ms" << std::endl;
    }
};

} // namespace driver
} // namespace gpu
} // namespace MIGRAPHX_INLINE_NS
} // namespace migraphx