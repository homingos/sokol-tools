/*
    Generator base class implementation.
*/
#include "generator.h"
#include "util.h"
#include "pystring.h"

using namespace shdc::gen::util;
using namespace shdc::refl;

namespace shdc::gen {

ErrMsg Generator::generate(const GenInput& gen) {
    ErrMsg err;
    err = begin(gen);
    if (err.valid()) {
        return err;
    }
    gen_prolog(gen);
    // FIXME: instead of the first valid Slang, the GenInput struct should
    // have a 'common reflection' object
    const Slang::Enum slang = Slang::first_valid(gen.args.slang);
    gen_header(gen, slang);
    gen_prerequisites(gen);
    gen_vertex_attr_consts(gen, slang);
    gen_bind_slot_consts(gen);
    gen_uniformblock_decls(gen);
    gen_stb_impl_start(gen);
    gen_shader_arrays(gen);
    gen_shader_desc_funcs(gen);
    if (gen.args.reflection) {
        gen_reflection_funcs(gen);
    }
    gen_epilog(gen);
    gen_stb_impl_end(gen);
    err = end(gen);
    return err;
}

Generator::ShaderStageInfo Generator::get_shader_stage_info(const GenInput& gen, const Program& prog, ShaderStage::Enum stage, Slang::Enum slang) {
    ShaderStageInfo info;
    if (stage == ShaderStage::VS) {
        info.stage_name = "vs";
        info.source = find_spirvcross_source_by_shader_name(prog.vs_name, gen.inp, gen.spirvcross[slang]);
        info.bytecode = find_bytecode_blob_by_shader_name(prog.vs_name, gen.inp, gen.bytecode[slang]);
        info.source_array_name = shader_source_array_name(prog.vs_name, slang);
        info.bytecode_array_name = shader_bytecode_array_name(prog.vs_name, slang);
    } else {
        info.stage_name = "fs";
        info.source = find_spirvcross_source_by_shader_name(prog.fs_name, gen.inp, gen.spirvcross[slang]);
        info.bytecode = find_bytecode_blob_by_shader_name(prog.fs_name, gen.inp, gen.bytecode[slang]);
        info.source_array_name = shader_source_array_name(prog.fs_name, slang);
        info.bytecode_array_name = shader_bytecode_array_name(prog.fs_name, slang);
    }
    return info;
}

// default behaviour of begin is to clear the generated content string, and check for error in GenInput
ErrMsg Generator::begin(const GenInput& gen) {
    content.clear();
    mod_prefix = util::mod_prefix(gen.inp);
    return check_errors(gen);
}

// create a default comment header
void Generator::gen_header(const GenInput& gen, Slang::Enum slang) {
    l("{}\n", comment_block_start());
    cbl("#version:{} (machine generated, don't edit!)\n\n", gen.args.gen_version);
    cbl("Generated by sokol-shdc (https://github.com/floooh/sokol-tools)\n\n");
    cbl("Cmdline:\n");
    cbl("    {}\n\n", gen.args.cmdline);

    // FIXME: instead of the first valid slang, GenInput should instead give us pre-processed and merged reflection info!
    cbl("Overview:\n");
    for (const auto& item: gen.inp.programs) {
        const Program& prog = item.second;
        const SpirvcrossSource* vs_src = find_spirvcross_source_by_shader_name(prog.vs_name, gen.inp, gen.spirvcross[slang]);
        const SpirvcrossSource* fs_src = find_spirvcross_source_by_shader_name(prog.fs_name, gen.inp, gen.spirvcross[slang]);
        assert(vs_src && fs_src);
        cbl("    Shader program: '{}':\n", prog.name);
        cbl("        Get shader desc: {}", get_shader_desc_help(prog.name));
        gen_vertex_shader_info(gen, prog, *vs_src);
        gen_fragment_shader_info(gen, prog, *fs_src);
    }
    l("{}\n", comment_block_end());
    l("\n");
}

void Generator::gen_vertex_shader_info(const GenInput& gen, const Program& prog, const SpirvcrossSource& src) {
    cbl("        Vertex shader: {}\n", prog.vs_name);
    cbl("            Attributes:\n");
    const Snippet& vs_snippet = gen.inp.snippets[src.snippet_index];
    for (const VertexAttr& attr: src.refl.inputs) {
        if (attr.slot >= 0) {
            cbl("                {} => {}\n", vertex_attr_name(vs_snippet.name, attr), attr.slot);
        }
    }
    gen_bindings_info(gen, src.refl.bindings);
}

void Generator::gen_fragment_shader_info(const GenInput& gen, const Program& prog, const SpirvcrossSource& src) {
    cbl("        Fragment shader: {}\n", prog.fs_name);
    gen_bindings_info(gen, src.refl.bindings);
}

void Generator::gen_bindings_info(const GenInput& gen, const Bindings& bindings) {
    for (const UniformBlock& ub: bindings.uniform_blocks) {
        cbl("            Uniform block '{}':\n", ub.struct_name);
        cbl("                {} struct: {}\n", lang_name(), struct_name(ub.struct_name));
        cbl("                Bind slot: {} => {}\n", uniform_block_bind_slot_name(ub), ub.slot);
    }
    for (const Image& img: bindings.images) {
        cbl("            Image '{}':\n", img.name);
        cbl("                Image type: {}\n", image_type(img.type));
        cbl("                Sample type: {}\n", image_sample_type(img.sample_type));
        cbl("                Multisampled: {}\n", img.multisampled);
        cbl("                Bind slot: {} => {}\n", image_bind_slot_name(img), img.slot);
    }
    for (const Sampler& smp: bindings.samplers) {
        cbl("            Sampler '{}':\n", smp.name);
        cbl("                Type: {}\n", sampler_type(smp.type));
        cbl("                Bind slot: {} => {}\n", sampler_bind_slot_name(smp), smp.slot);
    }
    for (const ImageSampler& img_smp: bindings.image_samplers) {
        cbl("            Image Sampler Pair '{}':\n", img_smp.name);
        cbl("                Image: {}\n", img_smp.image_name);
        cbl("                Sampler: {}\n", img_smp.sampler_name);
    }
}

void Generator::gen_vertex_attr_consts(const GenInput& gen, Slang::Enum slang) {
    for (const SpirvcrossSource& src: gen.spirvcross[slang].sources) {
        if (src.refl.stage == ShaderStage::VS) {
            const Snippet& vs_snippet = gen.inp.snippets[src.snippet_index];
            for (const VertexAttr& attr: src.refl.inputs) {
                if (attr.slot >= 0) {
                    l("{}\n", vertex_attr_definition(vs_snippet.name, attr));
                }
            }
        }
    }
    l("\n");
}

void Generator::gen_bind_slot_consts(const GenInput& gen) {
    for (const UniformBlock& ub: gen.merged_bindings.uniform_blocks) {
        l("{}\n", uniform_block_bind_slot_definition(ub));
    }
    l("\n");
    for (const Image& img: gen.merged_bindings.images) {
        l("{}\n", image_bind_slot_definition(img));
    }
    l("\n");
    for (const Sampler& smp: gen.merged_bindings.samplers) {
        l("{}\n", sampler_bind_slot_definition(smp));
    }
    l("\n");
}

void Generator::gen_uniformblock_decls(const GenInput& gen) {
    for (const UniformBlock& ub: gen.merged_bindings.uniform_blocks) {
        gen_uniformblock_decl(gen, ub);
    }
}

void Generator::gen_shader_arrays(const GenInput& gen) {
    for (int slang_idx = 0; slang_idx < Slang::NUM; slang_idx++) {
        Slang::Enum slang = Slang::from_index(slang_idx);
        if (gen.args.slang & Slang::bit(slang)) {
            const Spirvcross& spirvcross = gen.spirvcross[slang];
            const Bytecode& bytecode = gen.bytecode[slang];
            for (int snippet_index = 0; snippet_index < (int)gen.inp.snippets.size(); snippet_index++) {
                const Snippet& snippet = gen.inp.snippets[snippet_index];
                if ((snippet.type != Snippet::VS) && (snippet.type != Snippet::FS)) {
                    continue;
                }
                int src_index = spirvcross.find_source_by_snippet_index(snippet_index);
                assert(src_index >= 0);
                const SpirvcrossSource& src = spirvcross.sources[src_index];
                int blob_index = bytecode.find_blob_by_snippet_index(snippet_index);
                const BytecodeBlob* blob = (blob_index != -1) ? &bytecode.blobs[blob_index] : nullptr;
                std::vector<std::string> lines;
                pystring::splitlines(src.source_code, lines);
                // first write the source code in a comment block
                l("{}\n", comment_block_start());
                for (const std::string& line: lines) {
                    cbl("{}\n", replace_C_comment_tokens(line));
                }
                l("{}\n", comment_block_end());
                if (blob) {
                    const std::string array_name = shader_bytecode_array_name(snippet.name, slang);
                    gen_shader_array_start(gen, array_name, blob->data.size(), slang);
                    const size_t len = blob->data.size();
                    for (size_t i = 0; i < len; i++) {
                        if ((i & 15) == 0) {
                            l("    ");
                        }
                        l("{},", blob->data[i]);
                        if ((i & 15) == 15) {
                            l("\n");
                        }
                    }
                    gen_shader_array_end(gen);
                } else {
                    // if no bytecode exists, write the source code, but also a byte array with a trailing 0
                    const std::string array_name = shader_source_array_name(snippet.name, slang);
                    const size_t len = src.source_code.length() + 1;
                    gen_shader_array_start(gen, array_name, len, slang);
                    for (size_t i = 0; i < len; i++) {
                        if ((i & 15) == 0) {
                            l("    ");
                        }
                        l("{},", (int)src.source_code[i]);
                        if ((i & 15) == 15) {
                            l("\n");
                        }
                    }
                    gen_shader_array_end(gen);
                }
            }
        }
    }
}

void Generator::gen_shader_desc_funcs(const GenInput& gen) {
    for (const auto& item: gen.inp.programs) {
        const Program& prog = item.second;
        gen_shader_desc_func(gen, prog);
    }
}

void Generator::gen_reflection_funcs(const GenInput& gen) {
    for (const auto& item: gen.inp.programs) {
        const Program& prog = item.second;
        gen_attr_slot_refl_func(gen, prog);
        gen_image_slot_refl_func(gen, prog);
        gen_sampler_slot_refl_func(gen, prog);
        gen_uniformblock_slot_refl_func(gen, prog);
        gen_uniformblock_size_refl_func(gen, prog);
        gen_uniform_offset_refl_func(gen, prog);
        gen_uniform_desc_refl_func(gen, prog);
    }
}

// default behaviour of end() is to write the output file
ErrMsg Generator::end(const GenInput& gen) {
    FILE* f = fopen(gen.args.output.c_str(), "w");
    if (!f) {
        return ErrMsg::error(gen.inp.base_path, 0, fmt::format("failed to open output file '{}'", gen.args.output));
    }
    fwrite(content.c_str(), content.length(), 1, f);
    fclose(f);
    return ErrMsg();
}

// check that each input shader has a vs and fs source
ErrMsg Generator::check_errors(const GenInput& gen) {
    for (int i = 0; i < Slang::NUM; i++) {
        Slang::Enum slang = Slang::from_index(i);
        if (gen.args.slang & Slang::bit(slang)) {
            for (const auto& item: gen.inp.programs) {
                const Program& prog = item.second;
                int vs_snippet_index = gen.inp.snippet_map.at(prog.vs_name);
                int fs_snippet_index = gen.inp.snippet_map.at(prog.fs_name);
                int vs_src_index = gen.spirvcross[i].find_source_by_snippet_index(vs_snippet_index);
                int fs_src_index = gen.spirvcross[i].find_source_by_snippet_index(fs_snippet_index);
                if (vs_src_index < 0) {
                    return gen.inp.error(gen.inp.snippets[vs_snippet_index].lines[0],
                        fmt::format("no generated '{}' source for vertex shader '{}' in program '{}'",
                        Slang::to_str(slang), prog.vs_name, prog.name));
                }
                if (fs_src_index < 0) {
                    return gen.inp.error(gen.inp.snippets[vs_snippet_index].lines[0],
                        fmt::format("no generated '{}' source for fragment shader '{}' in program '{}'",
                        Slang::to_str(slang), prog.fs_name, prog.name));
                }
            }
        }
    }
    // all ok
    return ErrMsg();
}

} // namespace
