//
// Created by Loulfy on 12/04/2025.
//

#pragma once

#include "graph.hpp"

#include <imgui-node-editor/imgui_node_editor.h>
#include <imgui_internal.h>
namespace ed = ax::NodeEditor;

namespace ler::render
{
//------------------------------------------------------------------------------
struct BlueprintNodeBuilder
{
    BlueprintNodeBuilder(ImTextureID texture = nullptr, int textureWidth = 0, int textureHeight = 0);

    void Begin(ed::NodeId id);
    void End();

    void Header(const ImVec4& color = ImVec4(1, 1, 1, 1));
    void EndHeader();

    void Input(ed::PinId id);
    void EndInput();

    void Middle();

    void Output(ed::PinId id);
    void EndOutput();

  private:
    enum class Stage
    {
        Invalid,
        Begin,
        Header,
        Content,
        Input,
        Output,
        Middle,
        End
    };

    bool SetStage(Stage stage);

    void Pin(ed::PinId id, ax::NodeEditor::PinKind kind);
    void EndPin();

    ImTextureID HeaderTextureId;
    int HeaderTextureWidth;
    int HeaderTextureHeight;
    ed::NodeId CurrentNodeId;
    Stage CurrentStage;
    ImU32 HeaderColor;
    ImVec2 NodeMin;
    ImVec2 NodeMax;
    ImVec2 HeaderMin;
    ImVec2 HeaderMax;
    ImVec2 ContentMin;
    ImVec2 ContentMax;
    bool HasHeader;
};

void ImGuiEx_BeginColumn();
void ImGuiEx_NextColumn();
void ImGuiEx_EndColumn();

struct LinkInfo
{
    ed::LinkId Id;
    ed::PinId InputId;
    ed::PinId OutputId;
};

ImVec2 operator+(const ImVec2& a, const ImVec2& b);
ImVec2 operator-(const ImVec2& a, const ImVec2& b);
ImGuiWindowFlags getWindowFlags();

class FrameGraphEditor final : public rhi::IRenderPass
{
  public:
    void create(const rhi::DevicePtr& device, const rhi::SwapChainPtr& swapChain) override
    {
        ed::Config config;
        config.SettingsFile = "Simple.json";
        m_context = ed::CreateEditor(&config);

        {
            RenderGraphNode& node = m_nodes.emplace_back();
            node.type = RP_Compute;
            node.name = "AcquireNextFrame";
            node.index = 1;

            RenderGraphTable& res = node.resources;
            res.outputs.emplace_back();
            res.outputs.back().name = "backBuffer";
            res.outputs.back().type = render::RR_RenderTarget;
            res.outputs.back().bindlessIndex = 31;
            res.outputs.back().linkGroup = 42;
        }

        {
            RenderGraphNode& node = m_nodes.emplace_back();
            node.type = RP_Compute;
            node.name = "Present";
            node.index = 2;

            RenderGraphTable& res = node.resources;
            res.inputs.emplace_back();
            res.inputs.back().name = "backBuffer";
            res.inputs.back().type = render::RR_RenderTarget;
            res.inputs.back().bindlessIndex = 32;
            res.inputs.back().linkGroup = 16;
        }

        {
            RenderGraphNode& node = m_nodes.emplace_back();
            node.name = "ForwardIndexed";
            node.index = 3;

            RenderGraphTable& res = node.resources;
            res.inputs.emplace_back();
            res.inputs.back().name = "renderTarget";
            res.inputs.back().type = render::RR_RenderTarget;
            res.inputs.back().bindlessIndex = 33;
            res.inputs.back().linkGroup = 42;

            /*res.outputs.emplace_back();
            res.outputs.back().name = "backBuffer";
            res.outputs.back().type = render::RR_RenderTarget;
            res.outputs.back().bindlessIndex = 34;
            res.outputs.back().linkGroup = 16;

            res.outputs.emplace_back();
            res.outputs.back().name = "albedo";
            res.outputs.back().type = render::RR_ConstantBuffer;
            res.outputs.back().bindlessIndex = 35;
            res.outputs.back().linkGroup = 16;*/
        }

        {
            RenderGraphNode& node = m_nodes.emplace_back();
            node.name = "FrustumCulling";
            node.index = 4;

            RenderGraphTable& res = node.resources;
            /*res.inputs.emplace_back();
            res.inputs.back().name = "frustum";
            res.inputs.back().type = render::RR_ConstantBuffer;
            res.inputs.back().bindlessIndex = 36;
            res.inputs.back().linkGroup = 42;*/

            res.outputs.emplace_back();
            res.outputs.back().name = "drawBuffer";
            res.outputs.back().type = render::RR_StorageBuffer;
            res.outputs.back().bindlessIndex = 37;
            res.outputs.back().linkGroup = 16;

            res.outputs.emplace_back();
            res.outputs.back().name = "countBuffer";
            res.outputs.back().type = render::RR_StorageBuffer;
            res.outputs.back().bindlessIndex = 38;
            res.outputs.back().linkGroup = 16;
        }
    }

    void DrawPinIcon(const RenderNode& pin, bool connected, int alpha)
    {
        ImColor color = ImColor(1.f, 0.f, 0.f);
        color.Value.w = alpha / 255.0f;

        switch (pin.type)
        {
        case RR_ReadOnlyBuffer:
        case RR_ConstantBuffer:
        case RR_StorageBuffer:
            color = ImColor(1.f, 0.f, 1.f);
            break;
        case RR_SampledTexture:
        case RR_StorageImage:
        case RR_RenderTarget:
        case RR_DepthWrite:
            color = ImColor(1.f, 0.f, 0.f);
            break;
        case RR_Raytracing:
            color = ImColor(1.f, 1.f, 0.f);
            break;
        }

        float m_PinIconSize = 16;
        ImVec2 size(static_cast<float>(m_PinIconSize), static_cast<float>(m_PinIconSize));

        if (ImGui::IsRectVisible(size))
        {
            auto cursorPos = ImGui::GetCursorScreenPos();
            auto drawList = ImGui::GetWindowDrawList();
            ImVec2 t;
            // cursorPos.x -= m_PinIconSize * 1.5f;
            cursorPos.y -= 0.95;
            t.x = cursorPos.x + size.x;
            t.y = cursorPos.y + size.y;
            DrawIcon(drawList, cursorPos, t, connected, ImColor(color), ImColor(32, 32, 32, alpha));
        }
    }

    void DrawIcon(ImDrawList* drawList, const ImVec2 a, const ImVec2 b, bool filled, ImU32 color, ImU32 innerColor)
    {
        auto rect = ImRect(a, b);
        auto rect_x = rect.Min.x;
        auto rect_y = rect.Min.y;
        auto rect_w = rect.Max.x - rect.Min.x;
        auto rect_h = rect.Max.y - rect.Min.y;
        auto rect_center_x = (rect.Min.x + rect.Max.x) * 0.5f;
        auto rect_center_y = (rect.Min.y + rect.Max.y) * 0.5f;
        auto rect_center = ImVec2(rect_center_x, rect_center_y);
        const auto outline_scale = rect_w / 24.0f;
        const auto extra_segments = static_cast<int>(2 * outline_scale); // for full circle

        auto triangleStart = rect_center_x + 0.32f * rect_w;

        auto rect_offset = -static_cast<int>(rect_w * 0.25f * 0.25f);

        rect.Min.x += rect_offset;
        rect.Max.x += rect_offset;
        rect_x += rect_offset;
        rect_center_x += rect_offset * 0.5f;
        rect_center.x += rect_offset * 0.5f;

        const auto c = rect_center;

        if (!filled)
        {
            const auto r = 0.5f * rect_w / 2.0f - 0.5f;

            if (innerColor & 0xFF000000)
                drawList->AddCircleFilled(c, r, innerColor, 12 + extra_segments);
            drawList->AddCircle(c, r, color, 12 + extra_segments, 2.0f * outline_scale);
        }
        else
        {
            drawList->AddCircleFilled(c, 0.5f * rect_w / 2.0f, color, 12 + extra_segments);
        }

        const auto triangleTip = triangleStart + rect_w * (0.45f - 0.32f);

        drawList->AddTriangleFilled(ImVec2(ceilf(triangleTip), rect_y + rect_h * 0.5f),
                                    ImVec2(triangleStart, rect_center_y + 0.15f * rect_h),
                                    ImVec2(triangleStart, rect_center_y - 0.15f * rect_h), color);
    }

    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command) override
    {
        auto& io = ImGui::GetIO();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        const auto windowBorderSize = ImGui::GetStyle().WindowBorderSize;
        const auto windowRounding   = ImGui::GetStyle().WindowRounding;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("Content", nullptr, getWindowFlags());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, windowBorderSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, windowRounding);

        //ImGui::Begin("Test");
        ImGui::Text("FPS: %.2f (%.2gms)", io.Framerate, 1000.0f / io.Framerate);

        ImGui::Separator();

        ed::SetCurrentEditor(m_context);
        ed::Begin("My Editor", ImVec2(0.0, 0.0f));

        // Start drawing nodes.
        /*ed::BeginNode(uniqueId++);
        ImGui::Text("Node A");
        ed::BeginPin(uniqueId++, ed::PinKind::Input);
        ImGui::Text("-> In");
        ed::EndPin();
        ImGui::SameLine();
        ed::BeginPin(uniqueId++, ed::PinKind::Output);
        ImGui::Text("Out ->");
        ed::EndPin();
        ed::EndNode();*/

        ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(8, 4, 8, 8));
        for (auto& node : m_nodes)
        {
            ed::NodeId id = node.index;
            ed::BeginNode(id);
            ImGui::PushID(id.AsPointer());
            auto HeaderMin = ImGui::GetCursorScreenPos();
            ImGui::Text("%s", node.name.c_str());
            ImGuiEx_BeginColumn();
            for (auto& pin : node.resources.inputs)
            {
                auto alpha = ImGui::GetStyle().Alpha;
                ed::BeginPin(pin.bindlessIndex, ed::PinKind::Input);
                ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
                ed::PinPivotSize(ImVec2(0, 0));
                // ed::PushStyleVar(ed::StyleVar_PivotAlignment, ImVec2(0, 0.5f));
                // ed::PushStyleVar(ed::StyleVar_PivotSize, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                DrawPinIcon(pin, IsPinLinked(pin.bindlessIndex), (int)(alpha * 255));
                ImGui::Text("   %12s", pin.name.c_str());
                // ed::PopStyleVar(2);
                ImGui::PopStyleVar();
                ed::EndPin();
            }
            ImGuiEx_NextColumn();
            for (auto& pin : node.resources.outputs)
            {
                auto alpha = ImGui::GetStyle().Alpha;
                ed::BeginPin(pin.bindlessIndex, ed::PinKind::Output);
                ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
                ed::PinPivotSize(ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                ImGui::Text("%12s", pin.name.c_str());
                ImGui::SameLine();
                DrawPinIcon(pin, IsPinLinked(pin.bindlessIndex), (int)(alpha * 255));
                ImGui::SameLine();
                ImGui::Text("   ");
                ImGui::PopStyleVar();
                ed::EndPin();
            }
            ImGuiEx_EndColumn();
            auto HeaderMax = ImGui::GetItemRectMax();
            ed::EndNode();

            HeaderMax.y = HeaderMin.y + 15;

            if (ImGui::IsItemVisible())
            {
                auto alpha = static_cast<int>(255 * ImGui::GetStyle().Alpha);

                auto drawList = ed::GetNodeBackgroundDrawList(id);

                const auto halfBorderWidth = ed::GetStyle().NodeBorderWidth * 0.5f;

                auto headerColor = ImColor(135, 85, 85, alpha);
                if ((HeaderMax.x > HeaderMin.x) && (HeaderMax.y > HeaderMin.y))
                {
                    drawList->AddRectFilled(HeaderMin - ImVec2(8 - halfBorderWidth, 4 - halfBorderWidth),
                                            HeaderMax + ImVec2(8 - halfBorderWidth, 0),
                                            headerColor, ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersTop);

                    //if (ContentMin.y > HeaderMax.y)
                    {
                        drawList->AddLine(ImVec2(HeaderMin.x - (8 - halfBorderWidth), HeaderMax.y - 0.5f),
                                          ImVec2(HeaderMax.x + (8 - halfBorderWidth), HeaderMax.y - 0.5f),
                                          ImColor(255, 255, 255, 96 * alpha / (3 * 255)), 1.0f);
                    }
                }
            }

            ImGui::PopID();
        }
        ed::PopStyleVar();

        // Submit Links
        for (auto& linkInfo : m_links)
            ed::Link(linkInfo.Id, linkInfo.InputId, linkInfo.OutputId);

        // Handle creation action, returns true if editor want to create new object (node or link)
        if (ed::BeginCreate())
        {
            ed::PinId inputPinId, outputPinId;
            if (ed::QueryNewLink(&inputPinId, &outputPinId))
            {
                // QueryNewLink returns true if editor want to create new link between pins.
                //
                // Link can be created only for two valid pins, it is up to you to
                // validate if connection make sense. Editor is happy to make any.
                //
                // Link always goes from input to output. User may choose to drag
                // link from output pin or input pin. This determine which pin ids
                // are valid and which are not:
                //   * input valid, output invalid - user started to drag new ling from input pin
                //   * input invalid, output valid - user started to drag new ling from output pin
                //   * input valid, output valid   - user dragged link over other pin, can be validated

                if (inputPinId && outputPinId) // both are valid, let's accept link
                {
                    // ed::AcceptNewItem() return true when user release mouse button.
                    if (ed::AcceptNewItem())
                    {
                        // Since we accepted new link, lets add one to our list of links.
                        m_links.push_back({ ed::LinkId(m_NextLinkId++), inputPinId, outputPinId });

                        // Draw new link.
                        ed::Link(m_links.back().Id, m_links.back().InputId, m_links.back().OutputId);
                    }

                    // You may choose to reject connection between these nodes
                    // by calling ed::RejectNewItem(). This will allow editor to give
                    // visual feedback by changing link thickness and color.
                }
            }
        }
        ed::EndCreate(); // Wraps up object c

        // Handle deletion action
        if (ed::BeginDelete())
        {
            // There may be many links marked for deletion, let's loop over them.
            ed::LinkId deletedLinkId;
            while (ed::QueryDeletedLink(&deletedLinkId))
            {
                // If you agree that link can be deleted, accept deletion.
                if (ed::AcceptDeletedItem())
                {
                    // Then remove link from your data.
                    for (int i = 0; i < m_links.size(); ++i)
                    {
                        if (m_links[i].Id == deletedLinkId)
                        {
                            if (m_links.size() > 1)
                            {
                                std::iter_swap(m_links.begin() + i, m_links.end() - 1);
                                m_links.pop_back();
                            }
                            else
                            {
                                m_links.clear();
                            }
                            break;
                        }
                    }
                }

                // You may reject link deletion by calling:
                // ed::RejectDeletedItem();
            }
        }
        ed::EndDelete(); // Wrap up deletion action

        ed::End();
        ed::SetCurrentEditor(nullptr);

        ImGui::PopStyleVar(2);
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    bool IsPinLinked(ed::PinId id)
    {
        if (!id)
            return false;

        for (auto& link : m_links)
            if (link.InputId == id || link.OutputId == id)
                return true;

        return false;
    }

  private:
    ed::EditorContext* m_context = nullptr;
    std::vector<RenderGraphNode> m_nodes;
    std::vector<LinkInfo> m_links;
    int m_NextLinkId = 100;
};
} // namespace ler::render