#include <iterator>
#include <migraphx/gpu/loop.hpp>
#include <migraphx/gpu/context.hpp>

namespace migraphx {
inline namespace MIGRAPHX_INLINE_NS {
namespace gpu {

shape hip_loop::compute_shape(std::vector<shape> inputs, std::vector<module_ref> mods) const
{
    auto offset = inputs.size() - mods.front()->get_output_shapes().size();
    inputs.erase(inputs.begin() + offset, inputs.end());
    inputs.erase(inputs.begin() + 3);
    inputs.erase(inputs.begin() + 1);
    return op.compute_shape(inputs, mods);
}

static std::pair<int, bool> get_name_index(const std::string& name, const std::string& param_prefix)
{
    auto loc = name.find(param_prefix);
    if(loc != std::string::npos)
    {
        int index = std::stoi(name.substr(loc + param_prefix.size()));
        return {index, true};
    }

    std::string out_prefix = "#output_";
    loc                    = name.find(out_prefix);
    if(loc != std::string::npos)
    {
        int index = std::stoi(name.substr(loc + out_prefix.size()));
        return {index, false};
    }

    return {-1, false};
}

argument
hip_loop::compute(const shape&,
                  const std::vector<argument>& args,
                  const std::vector<module_ref>& mods,
                  const std::function<std::vector<argument>(
                      module_ref&, const std::unordered_map<std::string, argument>&)>& run) const
{
    auto cpy_args = args;
    std::vector<argument> cpu_args;
    cpu_args.push_back(cpy_args.at(1));
    cpu_args.push_back(cpy_args.at(3));
    cpy_args.erase(cpy_args.begin() + 3);
    cpy_args.erase(cpy_args.begin() + 1);

    auto iter_num    = cpu_args.at(0).at<int64_t>();
    auto cond        = cpu_args.at(1).at<bool>();
    module_ref mod   = mods.at(0);
    auto mod_out_num = mod->get_output_shapes().size();
    auto input_num   = cpy_args.size() - mod_out_num;
    auto dep_num     = input_num - 2;
    std::cout << "dep_num = " << dep_num << std::endl;
    auto param_name_shapes   = mod->get_parameter_shapes();
    std::string param_prefix = "#" + mod->name() + "_in_";

    std::vector<argument> in_args(cpy_args.begin(), cpy_args.begin() + input_num);
    std::vector<argument> out_args(cpy_args.begin() + input_num, cpy_args.end());
    std::cout << "iter_num = " << iter_num << std::endl;
    for(int64_t iter = 0; (iter < iter_num) and cond; ++iter)
    {
        std::cout << "in_arg_size = " << in_args.size() << std::endl;
        std::cout << "iter_num = " << iter_num << ", loop = " << iter << std::endl;
        // copy iter num and cond to device memory
        (void)hipMemcpy(in_args.at(0).data(), &iter, sizeof(int64_t), hipMemcpyHostToDevice);
        (void)hipMemcpy(in_args.at(1).data(), &cond, sizeof(bool), hipMemcpyHostToDevice);

        // wrap up the inputs and outputs
        std::unordered_map<std::string, argument> params;
        for(auto pn : param_name_shapes)
        {
            auto name = pn.first;
            std::cout << "param_name = " << name << std::endl;
            auto io_index = get_name_index(name, param_prefix);
            assert(io_index.first != -1);
            // name is for input
            if(io_index.second)
            {
                params[name] = in_args.at(io_index.first);
                std::cout << "in_idx = " << io_index.first << ", name = " << name
                          << ", shape = " << params[name].get_shape() << std::endl;
                auto arg = migraphx::gpu::from_gpu(in_args.at(io_index.first));
                std::cout << "name = " << name << ", val = " << arg << std::endl;
            }
            else
            {
                if(io_index.first > dep_num)
                {
                    const auto& arg = out_args.at(io_index.first);
                    params[name]    = arg.load(pn.second, arg.data() + iter * pn.second.bytes());
                }
                else
                {
                    params[name] = out_args.at(io_index.first);
                    std::cout << "out_idx = " << io_index.first << ", name = " << name
                              << ", shape = " << params[name].get_shape() << std::endl;
                    auto arg = migraphx::gpu::from_gpu(out_args.at(io_index.first));
                    std::cout << "name = " << name << ", val = " << arg << std::endl;
                }
            }
        }

        auto mod_args = run(mod, params);
        std::cout << "mod_arg_num = " << mod_args.size() << std::endl;

        // copy back cond to be used next iteration
        (void)hipMemcpy(&cond, mod_args.at(0).data(), sizeof(bool), hipMemcpyDeviceToHost);
        std::cout << "cond = " << cond << std::endl << std::endl;
        std::copy(mod_args.begin(), mod_args.begin() + dep_num + 1, in_args.begin() + 1);
        std::cout << "out_args = " << std::endl;
        for(const auto& arg : mod_args)
        {
            auto cpu_arg = migraphx::gpu::from_gpu(arg);
            std::cout << "out_arg = " << cpu_arg << std::endl;
        }
        std::cout << std::endl;
    }

    out_args.erase(out_args.begin());

    return argument(out_args);
}

} // namespace gpu
} // namespace MIGRAPHX_INLINE_NS
} // namespace migraphx