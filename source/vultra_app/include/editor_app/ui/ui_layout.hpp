#pragma once

struct ImVec2;

namespace vultra_app::ui
{
    // Unified responsive layout primitives built on top of ImGui.
    //
    // The rule across the whole editor: when horizontal space runs short, controls are never
    // clipped or pushed off-screen -- the label and the control shrink instead, and runs of inline
    // controls wrap to the next line. These helpers make that the default so individual windows do
    // not each reinvent (and get wrong) their own SameLine math.
    //
    // Sizes here are in design pixels; callers pass design px and the helpers apply ui::dp().

    namespace layout
    {
        inline constexpr float kMinLabelWidthDp   = 32.0f; // a label column never narrower than this
        inline constexpr float kMinControlWidthDp = 56.0f; // a value control never narrower than this
        inline constexpr float kDefaultLabelDp    = 160.0f; // preferred label column width
    }

    // Resolve the label-column width for a label+control row whose total width is `availablePx`.
    // The label gets `preferredLabelPx` when it fits, but shrinks (the caller should ellipsize it)
    // so the control keeps at least kMinControlWidthDp; on a very narrow row the label bottoms out
    // at kMinLabelWidthDp and the control takes whatever remains. All values are real pixels.
    [[nodiscard]] float adaptiveLabelWidth(float availablePx, float preferredLabelPx);

    // Draw `label` as text, ellipsized to fit `widthPx`; the full text shows on hover when clipped.
    void labelEllipsized(const char* label, float widthPx);

    // Predicted on-screen width of a standard ImGui::Checkbox(label) / ImGui::Button(label) at the
    // current style -- used to decide wrapping before the item is drawn.
    [[nodiscard]] float checkboxWidth(const char* label);
    [[nodiscard]] float buttonWidth(const char* label);

    // Lays out a run of items left-to-right, inserting a line break before any item that would
    // overflow the content region instead of letting SameLine push it off-screen. Usage:
    //
    //   InlineFlow flow;                    // capture the right edge at the start of the run
    //   flow.next(ui::checkboxWidth(a));    // call immediately before each item
    //   ImGui::Checkbox(a, &va);
    //   flow.next(ui::checkboxWidth(b));
    //   ImGui::Checkbox(b, &vb);
    //
    // next() leaves the first item on the current line and, for each later item, calls SameLine()
    // only when the item fits; otherwise it lets the item fall onto a fresh line.
    class InlineFlow
    {
    public:
        InlineFlow();
        void next(float itemWidthPx);

    private:
        float m_RightEdgeX {0.0f};
        bool  m_First {true};
    };
} // namespace vultra_app::ui
