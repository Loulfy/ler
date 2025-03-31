//
// Created by Loulfy on 11/03/2025.
//

#include "importer.hpp"
#include "sys/utils.hpp"

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/idl.h>
#include <fstream>

namespace ler::pak
{
static constexpr int64_t ALIGNMENT = 64 * 1024; // 64 KB en bytes
static constexpr int64_t METADATA = ALIGNMENT * 4;

static void appendPadding(std::ofstream& outFile, uint64_t paddingSize)
{
    std::vector<char> paddingBuffer(paddingSize, 0);
    outFile.write(paddingBuffer.data(), static_cast<std::streamsize>(paddingSize));
}

static void alignOutput(std::ofstream& outFile, int64_t currentPos)
{
    std::streamsize padding = (ALIGNMENT - (currentPos % ALIGNMENT)) % ALIGNMENT;
    if (padding > 0)
        appendPadding(outFile, padding);
}

PakPacker::PakPacker(const fs::path& path) : m_builder(65536), m_outFile(path, std::ios::binary)
{
    for (auto& b : m_vertexBuffers)
        b.reserve(sys::C16Mio);
    m_indexBuffer.reserve(sys::C16Mio);

    // Reserve header
    appendPadding(m_outFile, METADATA);
}

static constexpr std::array<BufferType, 4> kVertexOrder = { BufferType_Position, BufferType_Texcoord, BufferType_Normal,
                                                            BufferType_Tangent };

void PakPacker::setParentDir(const fs::path& root)
{
    m_root = root;
    fs::path cachePath = sys::getHomeDir() / sys::PACKED_DIR / m_root.stem();
    log::info("Cache Directory: {}", cachePath.string());
}

void PakPacker::finish()
{
    Buffer buffer(BufferType_Index);
    int64_t currentPos = m_outFile.tellp();

    alignOutput(m_outFile, currentPos);
    currentPos = m_outFile.tellp();
    auto currentSize = static_cast<int64_t>(m_indexBuffer.size() * sizeof(uint32_t));
    m_entries.emplace_back(CreatePakEntry(m_builder, currentSize, currentPos, ResourceType_Buffer,
                                          m_builder.CreateStruct(buffer).Union()));
    m_outFile.write(reinterpret_cast<const char*>(m_indexBuffer.data()), currentSize);
    currentPos += currentSize;

    for (int i = 0; i < m_vertexBuffers.size(); ++i)
    {
        buffer = Buffer(kVertexOrder[i]);
        alignOutput(m_outFile, currentPos);
        currentPos = m_outFile.tellp();
        auto& b = m_vertexBuffers[i];
        currentSize = static_cast<int64_t>(b.size() * sizeof(aiVector3D));
        m_outFile.write(reinterpret_cast<const char*>(b.data()), currentSize);
        m_entries.emplace_back(CreatePakEntry(m_builder, currentSize, currentPos, ResourceType_Buffer,
                                              m_builder.CreateStruct(buffer).Union()));
        currentPos += currentSize;
    }

    m_outFile.flush();

    auto en = m_builder.CreateVector(m_entries);
    auto me = m_builder.CreateVectorOfStructs(m_meshVector);
    auto ma = m_builder.CreateVectorOfStructs(m_materialVector);
    auto in = m_builder.CreateVectorOfStructs(m_instanceVector);
    flatbuffers::Offset<PakArchive> archive = CreatePakArchive(m_builder, en, ma, in, me);
    FinishPakArchiveBuffer(m_builder, archive);

    /*flatbuffers::ToStringVisitor stringVisitor("\n", true, "  ", true);
    IterateFlatBuffer(m_builder.GetBufferPointer(), PakArchiveTypeTable(), &stringVisitor);
    std::string json = stringVisitor.s;
    log::info(json);*/

    log::info("FB Size: {}", m_builder.GetSize());
    assert(m_builder.GetSize() < METADATA);

    m_outFile.seekp(std::ios_base::beg);
    const char header[] = { 'L', 'E', 'P', 'K' };
    int64_t fbSize = m_builder.GetSize();
    m_outFile.write(header, sizeof(uint32_t));
    m_outFile.write(reinterpret_cast<const char*>(&fbSize), sizeof(int64_t));
    m_outFile.write(reinterpret_cast<const char*>(m_builder.GetBufferPointer()), fbSize);
    m_outFile.flush();
    m_outFile.close();
}

void PakPacker::concatenateFilesWithAlignment(std::ofstream& outFile, flatbuffers::FlatBufferBuilder& builder,
                                              std::vector<flatbuffers::Offset<PakEntry>>& entries)
{
    int64_t currentPos = m_outFile.tellp();
    entries.reserve(m_textureMap.size());
    for (auto& tex : std::views::values(m_textureMap))
    {
        std::string filename = tex.gpuFile.stem().string();
        auto t = CreateTexture(builder, builder.CreateString(filename), tex.width, tex.height, tex.mipLevels,
                               convertCMPFormat(tex.format));

        std::error_code ec;
        const auto currentSize = static_cast<uint64_t>(fs::file_size(tex.gpuFile, ec));
        if (ec.value())
        {
            log::error("File Not Found: " + ec.message());
            continue;
        }

        std::ifstream inFile(tex.gpuFile, std::ios::binary);
        alignOutput(outFile, currentPos);
        currentPos = outFile.tellp();
        outFile << inFile.rdbuf();
        log::info("Packing: {}", tex.gpuFile.string());
        if (currentPos % ALIGNMENT != 0)
            log::error("Padding is wrong");
        entries.emplace_back(CreatePakEntry(builder, currentSize, currentPos, ResourceType_Texture, t.Union()));
        currentPos += inFile.tellg();
    }
}
} // namespace ler::pak