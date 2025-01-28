#include <metal_stdlib>
using namespace metal;

static constant uint kIRArgumentBufferBindPoint                   = 2;
//static constant uint kIRArgumentBufferHullDomainBindPoint         = 3;
static constant uint kIRDescriptorHeapBindPoint                   = 0;
//static constant uint kIRSamplerHeapBindPoint                      = 1;
static constant uint kIRArgumentBufferDrawArgumentsBindPoint      = 4;
static constant uint kIRArgumentBufferUniformsBindPoint           = 5;
static constant uint kIRVertexBufferBindPoint                     = 6;

struct IRRuntimeDrawIndexedArgument
{
    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int  baseVertexLocation;
    uint startInstanceLocation;
};

struct TopLevelArgument
{
    uint64_t cbvAddr;
    uint32_t drawId;
};

// This is the argument buffer that contains the ICB.
struct ICBContainer
{
    command_buffer commandBuffer [[ id(0) ]];
    device uint16_t* uniforms [[ id(1) ]];
    device IRRuntimeDrawIndexedArgument* drawArgs [[ id(2) ]];
    device TopLevelArgument* topLevel [[ id(3) ]];
    //array<constant float*,5> vertices [[ id(4) ]];
};

struct Command
{
    uint drawId;
    uint countIndex;
    uint instanceCount;
    uint firstIndex;
    uint baseVertex;
    uint baseInstance;
    uint instId;
};

kernel void
encodeMultiDrawIndexedCommands(uint                 objectIndex    [[ thread_position_in_grid ]],
                               constant Command     *cmd           [[ buffer(5) ]],
                               constant uint        *indices       [[ buffer(3) ]],
                               constant uint64_t    *cbvAddr       [[ buffer(2) ]],
                               constant uint8_t     *descHeap      [[ buffer(0) ]],
                               constant float       *vertices      [[ buffer(6) ]],
                               device ICBContainer  *icb_container [[ buffer(4) ]])
{
    render_command rc(icb_container->commandBuffer, objectIndex);
    
    /*
    // Set TopLevel arguments:
    // - 32BitConstants ShaderRegister 0 (Num32BitValues = 32)
    for(uint i = 0; i < 32; ++i)
        topLevel[i + 33 * objectIndex] = topLevel[i];
    
    // - 32BitConstants ShaderRegister 1 (Num32BitValues = 1)
    uint off = 33 * objectIndex + 32;
    topLevel[off] = cmd[objectIndex].drawId;
    
    // Set Uniforms: 2 = IndexTypeUInt32
    device uint16_t* uniforms = icb_container->uniforms + objectIndex;
    *uniforms = 2;*/
    
    /*device uint* topLevel = icb_container->topLevel + (objectIndex * 3);
    topLevel[0] = icb_container->topLevel[0];
    topLevel[1] = icb_container->topLevel[1];
    topLevel[2] = cmd[objectIndex].drawId;*/
    
    device TopLevelArgument* topLevel = icb_container->topLevel + objectIndex;
    topLevel->cbvAddr = *cbvAddr;
    topLevel->drawId = cmd[objectIndex].drawId;
    
    // Set Uniforms: 2 = IndexTypeUInt32
    device uint16_t* uniforms = icb_container->uniforms + objectIndex;
    *uniforms = 2;
    
    // Set Draw Indexed Argument
    device IRRuntimeDrawIndexedArgument* drawArgs = icb_container->drawArgs + objectIndex;
    drawArgs->indexCountPerInstance = cmd[objectIndex].countIndex;
    drawArgs->instanceCount = cmd[objectIndex].instanceCount;
    drawArgs->startIndexLocation = cmd[objectIndex].firstIndex * sizeof(uint32_t);
    drawArgs->baseVertexLocation = (int)cmd[objectIndex].baseVertex;
    drawArgs->startInstanceLocation = cmd[objectIndex].baseInstance;
    
    rc.set_vertex_buffer(descHeap, kIRDescriptorHeapBindPoint);
    rc.set_vertex_buffer(topLevel, kIRArgumentBufferBindPoint);
    rc.set_vertex_buffer(drawArgs, kIRArgumentBufferDrawArgumentsBindPoint);
    rc.set_vertex_buffer(uniforms, kIRArgumentBufferUniformsBindPoint);
    rc.set_vertex_buffer(vertices, kIRVertexBufferBindPoint);
    
    rc.draw_indexed_primitives(
            primitive_type::triangle,
            cmd[objectIndex].countIndex,
            indices + cmd[objectIndex].firstIndex,
            cmd[objectIndex].instanceCount,
            cmd[objectIndex].baseVertex,
            cmd[objectIndex].baseInstance);
}
