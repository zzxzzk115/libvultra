# Modern Render-Path Design (Draft)

## Goal
- Modern Techniques
- Fully GPU-Driven Pipeline

## FrameGraph

```mermaid
%%{init: {
  "themeVariables": {
    "fontSize": "12px",
    "nodePadding": "14px",
    "nodeBorderRadius": "8px",
    "nodeWidth": "180px",
    "primaryColor": "#1e293b",
    "primaryTextColor": "#f8fafc",
    "primaryBorderColor": "#334155",
    "lineColor": "#64748b"
  }
}}%%

graph TD
    %% Blackboard
    BB["Blackboard"]
    HIZ_PREV["HiZ[N-1]"]

    %% Frame N Nodes
    TS["Task Shader Culling"]
    MS["Mesh Shader Expansion"]
    VB["Visibility Buffer Pass"]
    DL["Deferred Lighting"]
    HZ["Hi-Z Rebuild"]

    %% Connections
    BB --> HIZ_PREV
    HIZ_PREV --> TS
    TS --> MS
    MS --> VB
    VB --> DL
    VB --> HZ
    HZ --> BB

    %% Classes
    classDef node fill:#1e293b,stroke:#334155,color:#f8fafc,rx:8,ry:8,font-weight:bold;
    classDef resource fill:#475569,stroke:#334155,color:#f1f5f9,rx:6,ry:6;
    classDef blackboard fill:#0f172a,stroke:#334155,color:#93c5fd,rx:6,ry:6;

    class TS,MS,VB,DL,HZ node;
    class HIZ_PREV resource;
    class BB blackboard;
```

## FrameGraph Node Responsibilities

| #     | Node                                       | Inputs                                                 | Outputs                                | Responsibilities                                                                                                                                  |
| ----- | ------------------------------------------ | ------------------------------------------------------ | -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| **1** | **Task Shader Culling**                           | • HiZ [N−1] <br>• Meshlet metadata <br>• Camera params | • Visible Meshlet List                 | Performs _GPU-driven culling_ (Frustum, Normal Cone, Hi-Z Occlusion) and selects proper LOD levels. Emits visible meshlets for the current frame. |
| **2** | **Mesh Shader Expansion**                  | • Visible Meshlet List <br>• Meshlet geometry buffers  | • Raster Primitives                    | Expands meshlets into vertices / triangles and submits them for hardware rasterization.                                                           |
| **3** | **Visibility Buffer Pass**                 | • Raster Primitives                                    | • Visibility Buffer <br>• Depth Buffer | Lightweight raster pass writing only Primitive ID + Material ID + Depth (replaces multi-MRT G-Buffer).                                            |
| **4** | **Deferred Lighting / Deferred Texturing** | • Visibility Buffer <br>• Material DB <br>• Light List | • Final Lit Color                      | Reconstructs surface data per pixel, fetches materials / textures, and performs BRDF lighting.                                                    |
| **5** | **Hi-Z Rebuild**                           | • Depth Buffer                                         | • HiZ [N]                              | Generates a new hierarchical Z pyramid from current frame depth and stores it back in the Blackboard for the next frame.                          |

## Blackboard / Persistent Resources

| Resource      | Lifetime                           | Description                                                                           |
| ------------- | ---------------------------------- | ------------------------------------------------------------------------------------- |
| **HiZ [N−1]** | Persistent (read-only this frame)  | Previous frame’s hierarchical Z buffer, used by **Task Shader Culling** for occlusion tests. |
| **HiZ [N]**   | Persistent (write-only this frame) | Rebuilt after rendering; becomes HiZ [N−1] for the next frame.                        |

## NOTE
- We don't need a Depth-PrePass since we are using Task + Mesh shaders to generate a V-Buffer.
- We don't implement Streaming (Virtual Geometries) for now for simplicity.