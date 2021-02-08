#include <migraphx/onnx/op_parser.hpp>
#include <migraphx/onnx/onnx_parser.hpp>
#include <migraphx/onnx/checks.hpp>
#include <migraphx/ranges.hpp>
#include <migraphx/instruction.hpp>
#include <migraphx/make_op.hpp>

namespace migraphx {
inline namespace MIGRAPHX_INLINE_NS {
namespace onnx {

struct parse_if : op_parser<parse_if>
{
    std::vector<op_desc> operators() const { return {{"If"}}; }

    std::vector<instruction_ref> parse(const op_desc& /*opd*/,
                                       onnx_parser& parser,
                                       const onnx_parser::node_info& info,
                                       std::vector<instruction_ref> args) const
    {
        const auto& then_graph = info.attributes.at("then_branch").g();
        std::string then_name  = info.name + "_then_graph";

        const auto& else_graph = info.attributes.at("else_branch").g();
        std::string else_name  = info.name + "_else_graph";

        migraphx::argument cond_arg = args.front()->eval();
        auto& map_insts             = info.node_insts;
        module_ref mdl              = info.mdl;

        // cond is not constant, need to create sub_modules
        if(cond_arg.empty())
        {
            module_ref then_mdl = parser.prog.create_module(then_name);
            then_mdl->set_parent_module(mdl);
            module_ref else_mdl = parser.prog.create_module(else_name);
            else_mdl->set_parent_module(mdl);

            // parse the then sub_graph
            parser.parse_graph(then_mdl, then_graph, map_insts);

            // parse_the else sub_graph
            parser.parse_graph(else_mdl, else_graph, map_insts);

            auto then_out_shapes = then_mdl->get_output_shapes();
            auto else_out_shapes = else_mdl->get_output_shapes();

            if(not std::equal(then_out_shapes.begin(),
                              then_out_shapes.end(),
                              else_out_shapes.begin(),
                              else_out_shapes.end()))
            {
                MIGRAPHX_THROW("PARSE_IF: then and else sub_grahps must have same output shapes!");
            }

            auto ret = info.add_instruction(
                make_op("iff", {{"then_sub_graph", then_name}, {"else_sub_graph", else_name}}),
                args,
                {then_mdl, else_mdl});

            return {ret};
        }
        else
        {
            std::vector<bool> vec_conds;
            cond_arg.visit([&](auto s) { vec_conds.assign(s.begin(), s.end()); });
            if(vec_conds.size() != 1)
            {
                MIGRAPHX_THROW("PARSE_IF: condition input can have only one element!");
            }

            // then branch
            if(vec_conds.front())
            {
                parser.parse_graph(mdl, then_graph, map_insts);
            }
            // else branch
            else
            {
                parser.parse_graph(mdl, else_graph, map_insts);
            }

            // inputs of the return instruction are that of the output of the
            // if instruction
            instruction_ref ret_ins = std::prev(mdl->end());
            auto outputs            = ret_ins->inputs();
            mdl->remove_instruction(ret_ins);

            return outputs;
        }
    }
};

} // namespace onnx
} // namespace MIGRAPHX_INLINE_NS
} // namespace migraphx