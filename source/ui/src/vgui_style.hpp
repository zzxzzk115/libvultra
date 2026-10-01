#pragma once

namespace vultra::detail
{
    inline constexpr char kVGuiStyle[] = R"rcss(
@spritesheet vgui-default {
    src: "vgui://skin";
    resolution: 2 x;
    vgui-check: 0px 0px 32px 32px;
    vgui-chevron: 32px 0px 32px 32px;
    vgui-dot: 64px 0px 32px 32px;
    vgui-knob: 96px 0px 32px 32px;
    vgui-plus: 128px 0px 32px 32px;
}
body {
    color: #d6d9dc;
    font-size: 14px;
}
.vgui-panel {
    padding: 16px;
    color: #d6d9dc;
    background-color: #24272bee;
    border: 1px #454b52;
    border-radius: 6px;
}
.vgui-surface {
    padding: 8px;
    background-color: #30353a;
    border: 1px #515a63;
    border-radius: 5px;
}
.vgui-divider {
    height: 1px;
    background-color: #454b52;
}
.vgui-disc {
    display: inline-block;
    width: 18px;
    height: 18px;
    decorator: image(vgui-dot scale-none);
}
.vgui-outline {
    display: inline-block;
    width: 18px;
    height: 18px;
    border: 2px #829fbd;
    border-radius: 5px;
}
.vgui-badge {
    display: inline-block;
    padding: 3px 8px;
    color: #dce6ef;
    background-color: #384959;
    border-radius: 10px;
    font-size: 11px;
}
.vgui-heading {
    display: block;
    color: #a6adb5;
    font-size: 11px;
}
.vgui-title {
    display: block;
    color: #f0f1f2;
    font-size: 18px;
}
.vgui-row {
    display: block;
    margin: 0;
    padding: 9px 6px;
    border-bottom: 1px #454b52;
    color: #d6d9dc;
    font-size: 12px;
}
.vgui-row:hover {
    background-color: #333940;
    color: #ffffff;
}
button, input.button, input.submit {
    display: inline-block;
    padding: 8px 10px;
    color: #d6d9dc;
    background-color: #393e44;
    border: 1px #596169;
    border-radius: 5px;
    font-size: 12px;
}
button:hover, input.button:hover, input.submit:hover {
    color: #ffffff;
    background-color: #4a5159;
    border-color: #7d93a7;
}
button:active, input.button:active, input.submit:active {
    background-color: #53677a;
}
button[disabled], input.button[disabled], input.submit[disabled] {
    color: #858c93;
    background-color: #2a2e32;
    border-color: #41464b;
}
.vgui-icon-button {
    width: 28px;
    height: 28px;
    padding: 0;
    decorator: image(vgui-plus scale-none);
}
input.checkbox, input.radio {
    display: inline-block;
    width: 18px;
    height: 18px;
    background-color: #1c2024;
    border: 2px #8b959e;
    border-radius: 4px;
}
input.radio {
    border-radius: 9px;
}
input.checkbox:checked {
    background-color: #829fbd;
    border-color: #e8edf2;
    decorator: image(vgui-check scale-none);
}
input.radio:checked {
    background-color: #829fbd;
    border-color: #e8edf2;
    decorator: image(vgui-dot scale-none);
}
input.checkbox:focus, input.radio:focus {
    border-color: #ffffff;
}
input.checkbox[disabled], input.radio[disabled] {
    background-color: #34393e;
    border-color: #636b72;
}
input.text, input.password, textarea {
    display: inline-block;
    width: 188px;
    padding: 7px 8px;
    color: #e2e5e8;
    background-color: #1c2024;
    border: 1px #596169;
    border-radius: 5px;
    font-size: 12px;
}
input.text:focus, input.password:focus, textarea:focus {
    border-color: #9bb2ca;
}
input.text[disabled], input.password[disabled], textarea[disabled] {
    color: #858c93;
    background-color: #2a2e32;
}
textarea {
    height: 58px;
}
select {
    width: 206px;
    height: 30px;
    color: #e2e5e8;
    background-color: #1c2024;
    border: 1px #596169;
    border-radius: 5px;
    font-size: 12px;
}
select selectvalue {
    height: 22px;
    padding: 6px 7px 0 7px;
    margin-right: 23px;
}
select selectarrow {
    width: 22px;
    height: 28px;
    background-color: #4a5159;
    border-left: 1px #596169;
    border-radius: 0 5px 5px 0;
    color: #e2e5e8;
    decorator: image(vgui-chevron scale-none);
}
select selectarrow:hover, select:checked selectarrow {
    background-color: #53677a;
}
select selectbox {
    width: 204px;
    max-height: 160px;
    overflow-y: auto;
    background-color: #2b3035;
    border: 1px #596169;
    border-radius: 5px;
}
select option {
    padding: 5px 7px;
}
select option:hover, select option:checked {
    background-color: #4a5159;
}
input.range {
    width: 206px;
    height: 20px;
}
input.range sliderarrowdec, input.range sliderarrowinc {
    width: 0;
    height: 0;
}
input.range slidertrack {
    height: 4px;
    margin-top: 8px;
    background-color: #454b52;
    border-radius: 2px;
}
/* RmlUi sizes progress to the knob's left edge; extend it beneath the knob. */
input.range sliderprogress {
    padding-right: 16px;
    background-color: #7894b1;
    border-radius: 2px;
}
input.range sliderbar {
    width: 16px;
    height: 16px;
    decorator: image(vgui-knob scale-none);
}
input.range sliderbar:hover {
    image-color: #ffffff;
}
input.range sliderbar:active {
    image-color: #b9d4ed;
}
progress {
    width: 206px;
    height: 8px;
    background-color: #454b52;
    border-radius: 4px;
}
progress fill {
    background-color: #7894b1;
    border-radius: 4px;
}
scrollbarvertical {
    width: 9px;
}
scrollbarvertical sliderarrowdec, scrollbarvertical sliderarrowinc {
    width: 0;
    height: 0;
}
scrollbarvertical slidertrack {
    width: 9px;
    background-color: #30353a;
}
scrollbarvertical sliderbar {
    width: 7px;
    margin-left: 1px;
    min-height: 18px;
    background-color: #69757f;
    border-radius: 4px;
}
scrollbarvertical sliderbar:hover {
    background-color: #8594a0;
}
scrollbarhorizontal {
    height: 9px;
}
scrollbarhorizontal sliderarrowdec, scrollbarhorizontal sliderarrowinc {
    width: 0;
    height: 0;
}
scrollbarhorizontal slidertrack {
    height: 9px;
    background-color: #30353a;
}
scrollbarhorizontal sliderbar {
    height: 7px;
    min-width: 18px;
    background-color: #69757f;
    border-radius: 4px;
}
scrollbarhorizontal sliderbar:hover {
    background-color: #8594a0;
}
)rcss";
} // namespace vultra::detail
