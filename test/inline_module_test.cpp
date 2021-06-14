#include <migraphx/dead_code_elimination.hpp>
#include <migraphx/inline_module.hpp>
#include <migraphx/pass_manager.hpp>
#include <migraphx/instruction.hpp>
#include <basic_ops.hpp>
#include <migraphx/operators.hpp>
#include <migraphx/make_op.hpp>

#include <test.hpp>

void run_pass(migraphx::program& p)
{
    migraphx::run_passes(p, {migraphx::inline_module{}, migraphx::dead_code_elimination{}});
}

TEST_CASE(cannot_inline_both)
{
    auto create_program = [] {
        migraphx::program p;
        auto* mm = p.get_main_module();
        migraphx::shape sd{migraphx::shape::float_type, {2, 3}};
        auto x = mm->add_parameter("x", sd);

        std::vector<float> one(sd.elements(), 1);
        std::vector<float> two(sd.elements(), 2);

        auto* then_smod = p.create_module("then_smod");
        auto l1         = then_smod->add_literal(migraphx::literal{sd, one});
        auto r1         = then_smod->add_instruction(migraphx::make_op("add"), x, l1);
        then_smod->add_return({r1});

        auto* else_smod = p.create_module("else_smod");
        auto l2         = else_smod->add_literal(migraphx::literal{sd, two});
        auto r2         = else_smod->add_instruction(migraphx::make_op("mul"), x, l2);
        else_smod->add_return({r2});

        migraphx::shape s_cond{migraphx::shape::bool_type, {1}};
        auto cond = mm->add_parameter("cond", s_cond);
        auto ret  = mm->add_instruction(migraphx::make_op("if"), {cond}, {then_smod, else_smod});
        mm->add_return({ret});

        return p;
    };

    auto p = create_program();
    run_pass(p);

    EXPECT(p == create_program());
}

TEST_CASE(cannot_inline_one)
{
    auto create_program = [] {
        migraphx::program p;
        auto* mm = p.get_main_module();
        migraphx::shape cond_s{migraphx::shape::bool_type};
        migraphx::shape s{migraphx::shape::float_type, {5}};
        auto cond = mm->add_parameter("cond", cond_s);
        auto x    = mm->add_parameter("x", s);

        auto* then_mod           = p.create_module("If_0_if");
        std::vector<float> data1 = {1, 2, 3, 4, 5};
        auto l1                  = then_mod->add_literal(migraphx::literal(s, data1));
        then_mod->add_return({l1, x});

        auto* else_mod           = p.create_module("If_0_else");
        std::vector<float> data2 = {5, 4, 3, 2, 1};
        auto l2                  = else_mod->add_literal(migraphx::literal(s, data2));
        auto s2                  = else_mod->add_instruction(migraphx::make_op("add"), x, l2);
        else_mod->add_return({s2, l2});

        auto ret = mm->add_instruction(migraphx::make_op("if"), {cond}, {then_mod, else_mod});
        mm->add_return({ret});

        return p;
    };

    auto p = create_program();
    run_pass(p);

    EXPECT(p == create_program());
}

TEST_CASE(inline_subgraph)
{
    auto create_program = [] {
        migraphx::program p;
        auto* mm = p.get_main_module();
        migraphx::shape cond_s{migraphx::shape::bool_type};
        auto cond = mm->add_parameter("cond", cond_s);

        migraphx::shape s{migraphx::shape::float_type, {5}};

        auto* then_mod           = p.create_module("If_0_if");
        std::vector<float> data1 = {1, 2, 3, 4, 5};
        then_mod->add_literal(migraphx::literal(s, data1));

        auto* else_mod           = p.create_module("If_0_else");
        std::vector<float> data2 = {5, 4, 3, 2, 1};
        auto l2                  = else_mod->add_literal(migraphx::literal(s, data2));
        else_mod->add_return({l2});

        auto ret = mm->add_instruction(migraphx::make_op("if"), {cond}, {then_mod, else_mod});
        auto r   = mm->add_instruction(migraphx::make_op("get_tuple_elem", {{"index", 0}}), ret);
        mm->add_return({r});

        return p;
    };

    auto create_inlined = [] {
        migraphx::shape s{migraphx::shape::float_type, {5}};
        std::vector<float> data1      = {1, 2, 3, 4, 5};
        std::vector<float> data2      = {5, 4, 3, 2, 1};
        std::vector<float> vec_offset = {5, 5, 5, 5, 5};
        std::vector<float> vec_idx    = {5, 6, 7, 8, 9};
        migraphx::shape cond_s{migraphx::shape::bool_type};

        migraphx::program pi;
        auto* mm   = pi.get_main_module();
        auto cond  = mm->add_parameter("cond", cond_s);
        auto l1    = mm->add_literal(migraphx::literal(s, data1));
        auto l2    = mm->add_literal(migraphx::literal(s, data2));
        auto lidx  = mm->add_literal(migraphx::literal(s, vec_idx));
        auto loff  = mm->add_literal(migraphx::literal(s, vec_offset));
        auto icond = mm->add_instruction(
            migraphx::make_op("convert", {{"target_type", migraphx::shape::float_type}}), cond);
        auto mcond =
            mm->add_instruction(migraphx::make_op("multibroadcast", {{"output_lens", {5}}}), icond);
        auto co       = mm->add_instruction(migraphx::make_op("mul"), mcond, loff);
        auto fcond    = mm->add_instruction(migraphx::make_op("sub"), lidx, co);
        auto ins_cond = mm->add_instruction(
            migraphx::make_op("convert", {{"target_type", migraphx::shape::int32_type}}), fcond);
        auto cl = mm->add_instruction(migraphx::make_op("concat", {{"axis", 0}}), l1, l2);
        auto rl = mm->add_instruction(migraphx::make_op("reshape", {{"dims", {10}}}), cl);
        auto r  = mm->add_instruction(migraphx::make_op("gather", {{"axis", 0}}), rl, ins_cond);
        mm->add_return({r});

        return pi;
    };

    auto p = create_program();
    run_pass(p);

    EXPECT(p == create_inlined());
}

TEST_CASE(inline_if_test)
{
    auto create_program = [] {
        migraphx::program p;
        auto* mm = p.get_main_module();
        migraphx::shape sc{migraphx::shape::bool_type, {1}};
        auto cond = mm->add_literal(migraphx::literal(sc, {1}));
        migraphx::shape s{migraphx::shape::float_type, {2, 3}};
        std::vector<float> ones(s.elements(), 1.0f);
        auto l1                 = mm->add_literal(s, ones);
        std::vector<float> rand = {-1.26487, -2.42279, 0.990835, 1.63072, 0.812238, -0.174946};
        auto l2                 = mm->add_literal(s, rand);
        auto x                  = mm->add_parameter("x", s);
        auto sm                 = mm->add_instruction(migraphx::make_op("add"), l1, x);
        auto y                  = mm->add_parameter("y", s);

        auto* then_mod = p.create_module("If_5_if");
        auto rt        = then_mod->add_instruction(migraphx::make_op("add"), x, sm);
        then_mod->add_outline(s);
        then_mod->add_return({rt});

        auto* else_mod = p.create_module("If_5_else");
        auto re        = else_mod->add_instruction(migraphx::make_op("mul"), y, l2);
        else_mod->add_return({re});

        auto ret = mm->add_instruction(migraphx::make_op("if"), {cond}, {then_mod, else_mod});
        auto r   = mm->add_instruction(migraphx::make_op("get_tuple_elem", {{"index", 0}}), ret);
        mm->add_return({r});
        return p;
    };

    auto create_inline = [] {
        migraphx::program p;
        auto* mm = p.get_main_module();
        migraphx::shape s{migraphx::shape::float_type, {2, 3}};
        std::vector<float> ones(s.elements(), 1.0f);
        auto l1                 = mm->add_literal(s, ones);
        std::vector<float> rand = {-1.26487, -2.42279, 0.990835, 1.63072, 0.812238, -0.174946};
        mm->add_literal(s, rand);
        auto x  = mm->add_parameter("x", s);
        auto sm = mm->add_instruction(migraphx::make_op("add"), l1, x);
        mm->add_parameter("y", s);
        auto r = mm->add_instruction(migraphx::make_op("add"), x, sm);
        mm->add_return({r});

        return p;
    };

    auto p = create_program();
    run_pass(p);
    EXPECT(p == create_inline());
}

TEST_CASE(inline_else_test)
{
    auto create_program = [] {
        migraphx::program p;
        auto* mm = p.get_main_module();
        migraphx::shape sc{migraphx::shape::bool_type, {1}};
        auto cond = mm->add_literal(migraphx::literal(sc, {0}));
        migraphx::shape s{migraphx::shape::float_type, {2, 3}};
        std::vector<float> ones(s.elements(), 1.0f);
        auto l1                 = mm->add_literal(s, ones);
        std::vector<float> rand = {-1.26487, -2.42279, 0.990835, 1.63072, 0.812238, -0.174946};
        auto l2                 = mm->add_literal(s, rand);
        auto x                  = mm->add_parameter("x", s);
        auto y                  = mm->add_parameter("y", s);

        auto* then_mod = p.create_module("If_5_if");
        auto rt        = then_mod->add_instruction(migraphx::make_op("add"), x, l1);
        then_mod->add_return({rt});

        auto* else_mod = p.create_module("If_5_else");
        else_mod->add_parameter("e", s);
        else_mod->add_literal(migraphx::literal(s, ones));
        auto re = else_mod->add_instruction(migraphx::make_op("mul"), y, l2);
        else_mod->add_return({re});

        auto ret = mm->add_instruction(migraphx::make_op("if"), {cond}, {then_mod, else_mod});
        auto r   = mm->add_instruction(migraphx::make_op("get_tuple_elem", {{"index", 0}}), ret);
        mm->add_return({r});
        return p;
    };

    auto create_inline = [] {
        migraphx::program p;
        auto* mm = p.get_main_module();
        migraphx::shape s{migraphx::shape::float_type, {2, 3}};
        std::vector<float> ones(s.elements(), 1.0f);
        mm->add_literal(s, ones);
        std::vector<float> rand = {-1.26487, -2.42279, 0.990835, 1.63072, 0.812238, -0.174946};
        auto l2                 = mm->add_literal(s, rand);
        mm->add_parameter("x", s);
        auto y = mm->add_parameter("y", s);
        auto r = mm->add_instruction(migraphx::make_op("mul"), y, l2);
        mm->add_return({r});

        return p;
    };

    auto p = create_program();
    run_pass(p);
    EXPECT(p == create_inline());
}

TEST_CASE(if_recursive_test)
{
    auto create_program = [] {
        migraphx::program p;
        auto* mm = p.get_main_module();
        migraphx::shape cond_s{migraphx::shape::bool_type};
        migraphx::shape xs{migraphx::shape::float_type, {2, 3}};
        migraphx::shape ys{migraphx::shape::float_type, {3, 3}};
        std::vector<float> datax = {1, 2, 3, 4, 5, 6};
        std::vector<float> datay = {8, 7, 6, 5, 4, 3, 2, 1, 0};

        auto lx    = mm->add_literal(migraphx::literal(xs, datax));
        auto ly    = mm->add_literal(migraphx::literal(ys, datay));
        auto cond  = mm->add_literal(migraphx::literal(cond_s, {0}));
        auto x1    = mm->add_parameter("x1", xs);
        auto x2    = mm->add_parameter("x2", xs);
        auto y2    = mm->add_parameter("y2", ys);
        auto cond1 = mm->add_parameter("cond", cond_s);

        auto* then_mod = p.create_module("If_5_if");
        auto l1        = then_mod->add_literal(migraphx::literal(ys, datay));
        auto a1        = then_mod->add_instruction(migraphx::make_op("add"), x1, lx);
        then_mod->add_return({a1, l1});

        auto* then_mod1 = p.create_module("If_6_if");
        auto l11        = then_mod1->add_literal(migraphx::literal(ys, datay));
        auto a11        = then_mod1->add_instruction(migraphx::make_op("add"), x2, lx);
        then_mod1->add_return({a11, l11});

        auto* else_mod1 = p.create_module("If_6_else");
        auto l21        = else_mod1->add_literal(migraphx::literal(xs, datax));
        auto a21        = else_mod1->add_instruction(migraphx::make_op("mul"), y2, ly);
        else_mod1->add_return({l21, a21});

        auto* else_mod = p.create_module("If_5_else");
        auto l2        = else_mod->add_literal(migraphx::literal(xs, datax));
        auto a2 =
            else_mod->add_instruction(migraphx::make_op("if"), {cond1}, {then_mod1, else_mod1});
        auto a3 =
            else_mod->add_instruction(migraphx::make_op("get_tuple_elem", {{"index", 1}}), a2);
        else_mod->add_return({l2, a3});

        auto ret = mm->add_instruction(migraphx::make_op("if"), {cond}, {then_mod, else_mod});
        auto r   = mm->add_instruction(migraphx::make_op("get_tuple_elem", {{"index", 1}}), ret);
        mm->add_return({r});

        return p;
    };

    auto create_inline = [] {
        migraphx::program p;
        auto* mm = p.get_main_module();
        migraphx::shape cond_s{migraphx::shape::bool_type};
        migraphx::shape xs{migraphx::shape::float_type, {2, 3}};
        migraphx::shape ys{migraphx::shape::float_type, {3, 3}};
        std::vector<float> datax = {1, 2, 3, 4, 5, 6};
        std::vector<float> datay = {8, 7, 6, 5, 4, 3, 2, 1, 0};

        auto lx = mm->add_literal(migraphx::literal(xs, datax));
        auto ly = mm->add_literal(migraphx::literal(ys, datay));
        mm->add_parameter("x1", xs);
        auto x2    = mm->add_parameter("x2", xs);
        auto y2    = mm->add_parameter("y2", ys);
        auto cond1 = mm->add_parameter("cond", cond_s);

        auto* then_mod1 = p.create_module("If_6_if");
        auto l11        = then_mod1->add_literal(migraphx::literal(ys, datay));
        auto a11        = then_mod1->add_instruction(migraphx::make_op("add"), x2, lx);
        then_mod1->add_return({a11, l11});

        auto* else_mod1 = p.create_module("If_6_else");
        auto l21        = else_mod1->add_literal(migraphx::literal(xs, datax));
        auto a21        = else_mod1->add_instruction(migraphx::make_op("mul"), y2, ly);
        else_mod1->add_return({l21, a21});

        auto ret = mm->add_instruction(migraphx::make_op("if"), {cond1}, {then_mod1, else_mod1});
        auto r   = mm->add_instruction(migraphx::make_op("get_tuple_elem", {{"index", 1}}), ret);
        mm->add_return({r});

        return p;
    };

    auto p = create_program();
    run_pass(p);
    EXPECT(p == create_inline());
}

TEST_CASE(if_recursive_cond0_test)
{
    auto create_program = [] {
        migraphx::program p;
        auto* mm = p.get_main_module();
        migraphx::shape cond_s{migraphx::shape::bool_type};
        migraphx::shape xs{migraphx::shape::float_type, {2, 3}};
        migraphx::shape ys{migraphx::shape::float_type, {3, 3}};
        std::vector<float> datax = {1, 2, 3, 4, 5, 6};
        std::vector<float> datay = {8, 7, 6, 5, 4, 3, 2, 1, 0};

        auto lx   = mm->add_literal(migraphx::literal(xs, datax));
        auto ly   = mm->add_literal(migraphx::literal(ys, datay));
        auto cond = mm->add_literal(migraphx::literal(cond_s, {0}));
        auto x1   = mm->add_parameter("x1", xs);
        auto x2   = mm->add_parameter("x2", xs);
        auto y2   = mm->add_parameter("y2", ys);

        auto* then_mod = p.create_module("If_5_if");
        auto l1        = then_mod->add_literal(migraphx::literal(ys, datay));
        auto a1        = then_mod->add_instruction(migraphx::make_op("add"), x1, lx);
        then_mod->add_return({a1, l1});

        auto* then_mod1 = p.create_module("If_6_if");
        auto l11        = then_mod1->add_literal(migraphx::literal(ys, datay));
        auto a11        = then_mod1->add_instruction(migraphx::make_op("add"), x2, lx);
        then_mod1->add_return({a11, l11});

        auto* else_mod1 = p.create_module("If_6_else");
        auto l21        = else_mod1->add_literal(migraphx::literal(xs, datax));
        auto a21        = else_mod1->add_instruction(migraphx::make_op("mul"), y2, ly);
        else_mod1->add_return({l21, a21});

        auto* else_mod = p.create_module("If_5_else");
        auto l2        = else_mod->add_literal(migraphx::literal(xs, datax));
        auto a2 =
            else_mod->add_instruction(migraphx::make_op("if"), {cond}, {then_mod1, else_mod1});
        auto a3 =
            else_mod->add_instruction(migraphx::make_op("get_tuple_elem", {{"index", 1}}), a2);
        else_mod->add_return({l2, a3});

        auto ret = mm->add_instruction(migraphx::make_op("if"), {cond}, {then_mod, else_mod});
        auto r   = mm->add_instruction(migraphx::make_op("get_tuple_elem", {{"index", 1}}), ret);
        mm->add_return({r});

        return p;
    };

    auto create_inline = [] {
        migraphx::program p;
        auto* mm = p.get_main_module();
        migraphx::shape cond_s{migraphx::shape::bool_type};
        migraphx::shape xs{migraphx::shape::float_type, {2, 3}};
        migraphx::shape ys{migraphx::shape::float_type, {3, 3}};
        std::vector<float> datax = {1, 2, 3, 4, 5, 6};
        std::vector<float> datay = {8, 7, 6, 5, 4, 3, 2, 1, 0};

        mm->add_literal(migraphx::literal(xs, datax));
        auto ly = mm->add_literal(migraphx::literal(ys, datay));
        mm->add_parameter("x1", xs);
        mm->add_parameter("x2", xs);
        auto y2 = mm->add_parameter("y2", ys);
        auto m  = mm->add_instruction(migraphx::make_op("mul"), y2, ly);
        mm->add_return({m});

        return p;
    };

    auto p = create_program();
    run_pass(p);
    EXPECT(p == create_inline());
}

TEST_CASE(inline_tuple_true_test)
{
    auto create_program = [] {
        migraphx::program p;
        auto* mm = p.get_main_module();
        migraphx::shape sc{migraphx::shape::bool_type, {1}};
        auto cond = mm->add_literal(migraphx::literal(sc, {1}));
        migraphx::shape sd{migraphx::shape::float_type, {1}};
        auto l1 = mm->add_literal(migraphx::literal(sd, {1}));
        auto l2 = mm->add_literal(migraphx::literal(sd, {2}));
        auto l3 = mm->add_literal(migraphx::literal(sd, {3}));
        migraphx::shape sx{migraphx::shape::float_type, {1, 4}};
        migraphx::shape sy{migraphx::shape::float_type, {3, 4}};
        auto x = mm->add_parameter("x", sx);
        auto y = mm->add_parameter("y", sy);

        auto* then_mod = p.create_module("If_6_if");
        auto m1        = then_mod->add_instruction(
            migraphx::make_op("multibroadcast", {{"output_lens", {1, 4}}}), l1);
        auto add0 = then_mod->add_instruction(migraphx::make_op("add"), x, m1);
        auto m2   = then_mod->add_instruction(
            migraphx::make_op("multibroadcast", {{"output_lens", {3, 4}}}), l2);
        auto mul0 = then_mod->add_instruction(migraphx::make_op("mul"), y, m2);
        then_mod->add_return({add0, mul0});

        auto* else_mod = p.create_module("If_6_else");
        auto me1       = else_mod->add_instruction(
            migraphx::make_op("multibroadcast", {{"output_lens", {1, 4}}}), l3);
        auto mul1 = else_mod->add_instruction(migraphx::make_op("mul"), x, me1);
        auto me2  = else_mod->add_instruction(
            migraphx::make_op("multibroadcast", {{"output_lens", {3, 4}}}), l3);
        auto add1 = else_mod->add_instruction(migraphx::make_op("add"), y, me2);
        else_mod->add_return({mul1, add1});

        auto ret = mm->add_instruction(migraphx::make_op("if"), {cond}, {then_mod, else_mod});
        auto r0  = mm->add_instruction(migraphx::make_op("get_tuple_elem", {{"index", 0}}), ret);
        auto r1  = mm->add_instruction(migraphx::make_op("get_tuple_elem", {{"index", 1}}), ret);
        mm->add_return({r0, r1});

        return p;
    };
    auto create_inline = [] {
        migraphx::program p;
        auto* mm = p.get_main_module();

        migraphx::shape sd{migraphx::shape::float_type, {1}};
        auto l1 = mm->add_literal(migraphx::literal(sd, {1}));
        auto l2 = mm->add_literal(migraphx::literal(sd, {2}));
        mm->add_literal(migraphx::literal(sd, {3}));
        migraphx::shape sx{migraphx::shape::float_type, {1, 4}};
        migraphx::shape sy{migraphx::shape::float_type, {3, 4}};
        auto x = mm->add_parameter("x", sx);
        auto y = mm->add_parameter("y", sy);

        auto m1 =
            mm->add_instruction(migraphx::make_op("multibroadcast", {{"output_lens", {1, 4}}}), l1);
        auto add = mm->add_instruction(migraphx::make_op("add"), x, m1);
        auto m2 =
            mm->add_instruction(migraphx::make_op("multibroadcast", {{"output_lens", {3, 4}}}), l2);
        auto mul = mm->add_instruction(migraphx::make_op("mul"), y, m2);
        mm->add_return({add, mul});

        return p;
    };

    auto p = create_program();
    run_pass(p);
    EXPECT(p == create_inline());
}

TEST_CASE(inline_tuple_false_test)
{
    auto create_program = [] {
        migraphx::program p;
        auto* mm = p.get_main_module();
        migraphx::shape sc{migraphx::shape::bool_type, {1}};
        auto cond = mm->add_literal(migraphx::literal(sc, {0}));
        migraphx::shape sd{migraphx::shape::float_type, {1}};
        auto l1 = mm->add_literal(migraphx::literal(sd, {1}));
        auto l2 = mm->add_literal(migraphx::literal(sd, {2}));
        auto l3 = mm->add_literal(migraphx::literal(sd, {3}));
        migraphx::shape sx{migraphx::shape::float_type, {1, 4}};
        migraphx::shape sy{migraphx::shape::float_type, {3, 4}};
        auto x = mm->add_parameter("x", sx);
        auto y = mm->add_parameter("y", sy);

        auto* then_mod = p.create_module("If_6_if");
        auto m1        = then_mod->add_instruction(
            migraphx::make_op("multibroadcast", {{"output_lens", {1, 4}}}), l1);
        auto add0 = then_mod->add_instruction(migraphx::make_op("add"), x, m1);
        auto m2   = then_mod->add_instruction(
            migraphx::make_op("multibroadcast", {{"output_lens", {3, 4}}}), l2);
        auto mul0 = then_mod->add_instruction(migraphx::make_op("mul"), y, m2);
        then_mod->add_return({add0, mul0});

        auto* else_mod = p.create_module("If_6_else");
        auto me1       = else_mod->add_instruction(
            migraphx::make_op("multibroadcast", {{"output_lens", {1, 4}}}), l3);
        auto mul1 = else_mod->add_instruction(migraphx::make_op("mul"), x, me1);
        auto me2  = else_mod->add_instruction(
            migraphx::make_op("multibroadcast", {{"output_lens", {3, 4}}}), l3);
        auto add1 = else_mod->add_instruction(migraphx::make_op("add"), y, me2);
        else_mod->add_return({mul1, add1});

        auto ret = mm->add_instruction(migraphx::make_op("if"), {cond}, {then_mod, else_mod});
        auto r0  = mm->add_instruction(migraphx::make_op("get_tuple_elem", {{"index", 0}}), ret);
        auto r1  = mm->add_instruction(migraphx::make_op("get_tuple_elem", {{"index", 1}}), ret);
        mm->add_return({r0, r1});

        return p;
    };

    auto create_inline = [] {
        migraphx::program p;
        auto* mm = p.get_main_module();

        migraphx::shape sc{migraphx::shape::bool_type, {1}};
        migraphx::shape sd{migraphx::shape::float_type, {1}};
        mm->add_literal(migraphx::literal(sd, {1}));
        mm->add_literal(migraphx::literal(sd, {2}));
        auto l3 = mm->add_literal(migraphx::literal(sd, {3}));
        migraphx::shape sx{migraphx::shape::float_type, {1, 4}};
        migraphx::shape sy{migraphx::shape::float_type, {3, 4}};
        auto x = mm->add_parameter("x", sx);
        auto y = mm->add_parameter("y", sy);

        auto m1 =
            mm->add_instruction(migraphx::make_op("multibroadcast", {{"output_lens", {1, 4}}}), l3);
        auto mul = mm->add_instruction(migraphx::make_op("mul"), x, m1);
        auto m2 =
            mm->add_instruction(migraphx::make_op("multibroadcast", {{"output_lens", {3, 4}}}), l3);
        auto add = mm->add_instruction(migraphx::make_op("add"), y, m2);
        mm->add_return({mul, add});

        return p;
    };

    auto p = create_program();
    run_pass(p);
    EXPECT(p == create_inline());
}

int main(int argc, const char* argv[]) { test::run(argc, argv); }