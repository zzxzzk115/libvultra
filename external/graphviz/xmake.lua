target("graphviz")
    set_kind("static")
    set_languages("c11")

    add_headerfiles("*.h")
    add_headerfiles("lib/cdt/*.h", "lib/cgraph/*.h", "lib/gvc/*.h", "lib/pathplan/*.h")
    add_headerfiles("lib/common/*.h", "lib/dotgen/*.h", "lib/xdot/*.h")

    add_includedirs(".", {public = true})
    add_includedirs("lib", {public = true})
    add_includedirs("lib/cdt", {public = true})
    add_includedirs("lib/cgraph", {public = true})
    add_includedirs("lib/gvc", {public = true})
    add_includedirs("lib/pathplan", {public = true})
    add_includedirs("lib/common", {public = true})
    add_includedirs("lib/dotgen")
    add_includedirs("lib/label")
    add_includedirs("lib/pack")
    add_includedirs("lib/xdot")
    add_includedirs("plugin/core")
    add_includedirs("plugin/dot_layout")

    if is_plat("windows") then
        add_includedirs("windows/include/unistd")
        add_cflags("/std:c11", "/experimental:c11atomics", "/utf-8", "/wd4244", "/wd4267", "/wd4996")
    end

    add_files("vultra_graphviz_plugins.c")

    add_files(
        "lib/cdt/dtclose.c",
        "lib/cdt/dtdisc.c",
        "lib/cdt/dtextract.c",
        "lib/cdt/dtflatten.c",
        "lib/cdt/dthash.c",
        "lib/cdt/dtmethod.c",
        "lib/cdt/dtopen.c",
        "lib/cdt/dtrenew.c",
        "lib/cdt/dtrestore.c",
        "lib/cdt/dtsize.c",
        "lib/cdt/dtstat.c",
        "lib/cdt/dtstrhash.c",
        "lib/cdt/dttree.c",
        "lib/cdt/dtview.c",
        "lib/cdt/dtwalk.c")

    add_files(
        "lib/cgraph/acyclic.c",
        "lib/cgraph/agerror.c",
        "lib/cgraph/apply.c",
        "lib/cgraph/attr.c",
        "lib/cgraph/edge.c",
        "lib/cgraph/graph.c",
        "lib/cgraph/grammar.c",
        "lib/cgraph/id.c",
        "lib/cgraph/imap.c",
        "lib/cgraph/ingraphs.c",
        "lib/cgraph/io.c",
        "lib/cgraph/node.c",
        "lib/cgraph/node_induce.c",
        "lib/cgraph/obj.c",
        "lib/cgraph/rec.c",
        "lib/cgraph/refstr.c",
        "lib/cgraph/scan.c",
        "lib/cgraph/subg.c",
        "lib/cgraph/tred.c",
        "lib/cgraph/unflatten.c",
        "lib/cgraph/utils.c",
        "lib/cgraph/write.c")

    add_files(
        "lib/pathplan/cvt.c",
        "lib/pathplan/inpoly.c",
        "lib/pathplan/route.c",
        "lib/pathplan/shortest.c",
        "lib/pathplan/shortestpth.c",
        "lib/pathplan/solvers.c",
        "lib/pathplan/triang.c",
        "lib/pathplan/util.c",
        "lib/pathplan/visibility.c")

    add_files(
        "lib/pack/ccomps.c",
        "lib/pack/pack.c",
        "lib/label/index.c",
        "lib/label/node.c",
        "lib/label/rectangle.c",
        "lib/label/split.q.c",
        "lib/label/xlabels.c",
        "lib/xdot/xdot.c")

    add_files(
        "lib/util/gv_fopen.c",
        "lib/util/list.c",
        "lib/util/xml.c")

    add_files(
        "lib/common/arrows.c",
        "lib/common/args.c",
        "lib/common/colxlate.c",
        "lib/common/ellipse.c",
        "lib/common/emit.c",
        "lib/common/geom.c",
        "lib/common/globals.c",
        "lib/common/htmllex.c",
        "lib/common/htmlparse.c",
        "lib/common/htmltable.c",
        "lib/common/input.c",
        "lib/common/labels.c",
        "lib/common/ns.c",
        "lib/common/output.c",
        "lib/common/pointset.c",
        "lib/common/postproc.c",
        "lib/common/psusershape.c",
        "lib/common/routespl.c",
        "lib/common/shapes.c",
        "lib/common/splines.c",
        "lib/common/taper.c",
        "lib/common/textspan.c",
        "lib/common/textspan_lut.c",
        "lib/common/timing.c",
        "lib/common/utils.c")

    add_files(
        "lib/dotgen/acyclic.c",
        "lib/dotgen/aspect.c",
        "lib/dotgen/class1.c",
        "lib/dotgen/class2.c",
        "lib/dotgen/cluster.c",
        "lib/dotgen/compound.c",
        "lib/dotgen/conc.c",
        "lib/dotgen/decomp.c",
        "lib/dotgen/dotinit.c",
        "lib/dotgen/dotsplines.c",
        "lib/dotgen/fastgr.c",
        "lib/dotgen/flat.c",
        "lib/dotgen/mincross.c",
        "lib/dotgen/position.c",
        "lib/dotgen/rank.c",
        "lib/dotgen/sameport.c")

    add_files(
        "lib/gvc/gvc.c",
        "lib/gvc/gvconfig.c",
        "lib/gvc/gvcontext.c",
        "lib/gvc/gvdevice.c",
        "lib/gvc/gvevent.c",
        "lib/gvc/gvjobs.c",
        "lib/gvc/gvlayout.c",
        "lib/gvc/gvloadimage.c",
        "lib/gvc/gvplugin.c",
        "lib/gvc/gvrender.c",
        "lib/gvc/gvtextlayout.c",
        "lib/gvc/gvtool_tred.c",
        "lib/gvc/gvusershape.c")

    add_files(
        "plugin/core/gvrender_core_dot.c",
        "plugin/dot_layout/gvlayout_dot_layout.c")

    if is_plat("android") then
        add_cflags("-fPIC")
    end
