//
// Created by Loulfy on 12/04/2025.
//

#include "graph_editor.hpp"
#include <imgui_internal.h>

namespace ler::render
{
ImVec2 operator+(const ImVec2& a, const ImVec2& b)
{
    ImVec2 res;
    res.x = a.x + b.x;
    res.y = a.y + b.y;
    return res;
}

ImVec2 operator-(const ImVec2& a, const ImVec2& b)
{
    ImVec2 res;
    res.x = a.x - b.x;
    res.y = a.y - b.y;
    return res;
}

void ImGuiEx_BeginColumn()
{
    ImGui::BeginGroup();
}

void ImGuiEx_NextColumn()
{
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
}

void ImGuiEx_EndColumn()
{
    ImGui::EndGroup();
}

void BeginHorizontal(const char* str_id, const ImVec2& size = ImVec2(0, 0), float align = -1.0f) {}
void BeginHorizontal(const void* ptr_id, const ImVec2& size = ImVec2(0, 0), float align = -1.0f) {}
void EndHorizontal() {}
void BeginVertical(const char* str_id, const ImVec2& size = ImVec2(0, 0), float align = -1.0f) {}
void EndVertical() {}
void Spring(float weight = 1.0f, float spacing = -1.0f) {}


BlueprintNodeBuilder::BlueprintNodeBuilder(ImTextureID texture, int textureWidth, int textureHeight)
    : HeaderTextureId(texture), HeaderTextureWidth(textureWidth), HeaderTextureHeight(textureHeight), CurrentNodeId(0),
      CurrentStage(Stage::Invalid), HasHeader(false)
{
}

void BlueprintNodeBuilder::Begin(ed::NodeId id)
{
    HasHeader = false;
    HeaderMin = HeaderMax = ImVec2();

    ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(8, 4, 8, 8));

    ed::BeginNode(id);

    ImGui::PushID(id.AsPointer());
    CurrentNodeId = id;

    SetStage(Stage::Begin);
}

void BlueprintNodeBuilder::End()
{
    SetStage(Stage::End);

    ed::EndNode();

    if (ImGui::IsItemVisible())
    {
        auto alpha = static_cast<int>(255 * ImGui::GetStyle().Alpha);

        auto drawList = ed::GetNodeBackgroundDrawList(CurrentNodeId);

        const auto halfBorderWidth = ed::GetStyle().NodeBorderWidth * 0.5f;

        auto headerColor = IM_COL32(0, 0, 0, alpha) | (HeaderColor & IM_COL32(255, 255, 255, 0));
        if ((HeaderMax.x > HeaderMin.x) && (HeaderMax.y > HeaderMin.y) && HeaderTextureId)
        {
            const auto uv = ImVec2((HeaderMax.x - HeaderMin.x) / (float)(4.0f * HeaderTextureWidth),
                                   (HeaderMax.y - HeaderMin.y) / (float)(4.0f * HeaderTextureHeight));
            /*
                        drawList->AddImageRounded(HeaderTextureId, HeaderMin - ImVec2(8 - halfBorderWidth, 4 -
            halfBorderWidth), HeaderMax + ImVec2(8 - halfBorderWidth, 0), ImVec2(0.0f, 0.0f), uv, #if IMGUI_VERSION_NUM
            > 18101 headerColor, GetStyle().NodeRounding, ImDrawFlags_RoundCornersTop); #else headerColor,
            GetStyle().NodeRounding, 1 | 2); #endif
            */
            if (ContentMin.y > HeaderMax.y)
            {
                drawList->AddLine(ImVec2(HeaderMin.x - (8 - halfBorderWidth), HeaderMax.y - 0.5f),
                                  ImVec2(HeaderMax.x + (8 - halfBorderWidth), HeaderMax.y - 0.5f),
                                  ImColor(255, 255, 255, 96 * alpha / (3 * 255)), 1.0f);
            }
        }
    }

    CurrentNodeId = 0;

    ImGui::PopID();

    ed::PopStyleVar();

    SetStage(Stage::Invalid);
}

void BlueprintNodeBuilder::Header(const ImVec4& color)
{
    HeaderColor = ImColor(color);
    SetStage(Stage::Header);
}

void BlueprintNodeBuilder::EndHeader()
{
    SetStage(Stage::Content);
}

void BlueprintNodeBuilder::Input(ed::PinId id)
{
    if (CurrentStage == Stage::Begin)
        SetStage(Stage::Content);

    const auto applyPadding = (CurrentStage == Stage::Input);

    SetStage(Stage::Input);

    if (applyPadding)
        Spring(0);

    Pin(id, ed::PinKind::Input);

    BeginHorizontal(id.AsPointer());
}

void BlueprintNodeBuilder::EndInput()
{
    EndHorizontal();

    EndPin();
}

void BlueprintNodeBuilder::Middle()
{
    if (CurrentStage == Stage::Begin)
        SetStage(Stage::Content);

    SetStage(Stage::Middle);
}

void BlueprintNodeBuilder::Output(ed::PinId id)
{
    if (CurrentStage == Stage::Begin)
        SetStage(Stage::Content);

    const auto applyPadding = (CurrentStage == Stage::Output);

    SetStage(Stage::Output);

    if (applyPadding)
        Spring(0);

    Pin(id, ed::PinKind::Output);

    BeginHorizontal(id.AsPointer());
}

void BlueprintNodeBuilder::EndOutput()
{
    EndHorizontal();

    EndPin();
}

bool BlueprintNodeBuilder::SetStage(Stage stage)
{
    if (stage == CurrentStage)
        return false;

    auto oldStage = CurrentStage;
    CurrentStage = stage;

    ImVec2 cursor;
    switch (oldStage)
    {
    case Stage::Begin:
        break;

    case Stage::Header:
        EndHorizontal();
        HeaderMin = ImGui::GetItemRectMin();
        HeaderMax = ImGui::GetItemRectMax();

        // spacing between header and content
        Spring(0, ImGui::GetStyle().ItemSpacing.y * 2.0f);

        break;

    case Stage::Content:
        break;

    case Stage::Input:
        ed::PopStyleVar(2);

        Spring(1, 0);
        EndVertical();

        // #debug
        // ImGui::GetWindowDrawList()->AddRect(
        //     ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 0, 0, 255));

        break;

    case Stage::Middle:
        EndVertical();

        // #debug
        // ImGui::GetWindowDrawList()->AddRect(
        //     ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 0, 0, 255));

        break;

    case Stage::Output:
        ed::PopStyleVar(2);

        Spring(1, 0);
        EndVertical();

        // #debug
        // ImGui::GetWindowDrawList()->AddRect(
        //     ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 0, 0, 255));

        break;

    case Stage::End:
        break;

    case Stage::Invalid:
        break;
    }

    switch (stage)
    {
    case Stage::Begin:
        BeginVertical("node");
        break;

    case Stage::Header:
        HasHeader = true;

        BeginHorizontal("header");
        break;

    case Stage::Content:
        if (oldStage == Stage::Begin)
            Spring(0);

        BeginHorizontal("content");
        Spring(0, 0);
        break;

    case Stage::Input:
        BeginVertical("inputs", ImVec2(0, 0), 0.0f);

        ed::PushStyleVar(ed::StyleVar_PivotAlignment, ImVec2(0, 0.5f));
        ed::PushStyleVar(ed::StyleVar_PivotSize, ImVec2(0, 0));

        if (!HasHeader)
            Spring(1, 0);
        break;

    case Stage::Middle:
        Spring(1);
        BeginVertical("middle", ImVec2(0, 0), 1.0f);
        break;

    case Stage::Output:
        if (oldStage == Stage::Middle || oldStage == Stage::Input)
            Spring(1);
        else
            Spring(1, 0);
        BeginVertical("outputs", ImVec2(0, 0), 1.0f);

        ed::PushStyleVar(ed::StyleVar_PivotAlignment, ImVec2(1.0f, 0.5f));
        ed::PushStyleVar(ed::StyleVar_PivotSize, ImVec2(0, 0));

        if (!HasHeader)
            Spring(1, 0);
        break;

    case Stage::End:
        if (oldStage == Stage::Input)
            Spring(1, 0);
        if (oldStage != Stage::Begin)
            EndHorizontal();
        ContentMin = ImGui::GetItemRectMin();
        ContentMax = ImGui::GetItemRectMax();

        Spring(0);
        EndVertical();
        NodeMin = ImGui::GetItemRectMin();
        NodeMax = ImGui::GetItemRectMax();
        break;

    case Stage::Invalid:
        break;
    }

    return true;
}

void BlueprintNodeBuilder::Pin(ed::PinId id, ed::PinKind kind)
{
    ed::BeginPin(id, kind);
}

void BlueprintNodeBuilder::EndPin()
{
    ed::EndPin();

    // #debug
    // ImGui::GetWindowDrawList()->AddRectFilled(
    //     ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 0, 0, 64));
}
} // namespace ler::render