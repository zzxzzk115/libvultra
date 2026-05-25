# Graphviz - Graph Visualization Tools

[![build status](https://gitlab.com/graphviz/graphviz/badges/main/pipeline.svg)](https://gitlab.com/graphviz/graphviz/-/pipelines/)

from AT&amp;T Research and Lucent Bell Labs

* https://graphviz.org/

See https://graphviz.org/doc/build.html for prerequisites and detailed build notes.

## main GIT Repository

The main GIT Repository for graphviz can be found at:

* https://gitlab.com/graphviz/graphviz/

## Support
Graphviz is maintained by volunteers. Most work is aimed at improving
the overall quality of the code (readability, consistency, organization,
and portability), modernizing the build toolchain, and supporting the
external audience and ecosystem for graphviz. This effort is supported
by an extensive regression test suite. Occasionally, work can
address new features, running time bottlenecks or specific bugs.

Meaningful bug reports are appreciated, but because resources are limited,
many reports will not be addressed individually. After years, the maintainers
have reduced open issues from thousands to several hundred. The Graphviz core
was written in an experimental style and is known to be not very resilient
to intentional attacks. We strongly recommend not exposing graphviz in a
potential attack surface, and it is of little benefit to submit batches of 
issues generated through automated fuzzing and ASAN testing.

## Documentation

The Graphviz documents are hosted at https://graphviz.org/

## Graph Visualization ( https://graphviz.org/about/ )

Graph visualization is a way of representing structural information as diagrams of abstract graphs and networks. It has important applications in networking, bioinformatics, software engineering, database and web design, machine learning, and in visual interfaces for other technical domains.

Graphviz is open source graph visualization software. It has several main layout programs. See the gallery for sample layouts. It also has web and interactive graphical interfaces, and auxiliary tools, libraries, and language bindings. We're not able to put a lot of work into GUI editors but there are quite a few external projects and even commercial tools that incorporate Graphviz. You can find some of these in the Resources section.

The Graphviz layout programs take descriptions of graphs in a simple text language, and make diagrams in useful formats, such as images and SVG for web pages; PDF or Postscript for inclusion in other documents; or display in an interactive graph browser.

Graphviz has many useful features for concrete diagrams, such as options for colors, fonts, tabular node layouts, line styles, hyperlinks, and custom shapes.

In practice, graphs are usually generated from an external data sources, but they can also be created and edited manually, either as raw text files or within a graphical editor. (Graphviz was not intended to be a Visio replacement, so it is probably frustrating to try to use it that way.)

## Contacts

If you have a bug or believe something is not working as expected, please submit a [bug report](https://gitlab.com/graphviz/graphviz/issues).
If you do not want to sign up for Gitlab, you can email bug reports to the
recent top committer
(`git shortlog --email --numbered --summary origin/main~100.. | head -1`).

If you have a general question or are unsure how things work, these queries can be posted in the [Graphviz Forum](https://forum.graphviz.org/).
