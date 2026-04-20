#ifndef OPENMW_COMPONENTS_TERRAIN_CHUNKMANAGER_H
#define OPENMW_COMPONENTS_TERRAIN_CHUNKMANAGER_H

#include <tuple>

#include <components/resource/resourcemanager.hpp>

#include "buffercache.hpp"
#include "quadtreeworld.hpp"

namespace osg
{
    class Group;
    class Texture2D;
}

namespace Resource
{
    class SceneManager;
}

namespace Terrain
{

    class TextureManager;
    class CompositeMapRenderer;
    class Storage;
    class CompositeMap;
    class TerrainDrawable;

    struct TerrainChunkTemplateId
    {
        osg::Vec2f mCenter;
        unsigned char mLod;
    };

    inline auto tie(const TerrainChunkTemplateId& v)
    {
        return std::tie(v.mCenter, v.mLod);
    }

    inline bool operator<(const TerrainChunkTemplateId& l, const TerrainChunkTemplateId& r)
    {
        return tie(l) < tie(r);
    }

    inline bool operator==(const TerrainChunkTemplateId& l, const TerrainChunkTemplateId& r)
    {
        return tie(l) == tie(r);
    }

    struct ChunkId
    {
        osg::Vec2f mCenter;
        unsigned char mLod;
        unsigned int mLodFlags;
    };

    inline auto tie(const ChunkId& v)
    {
        return std::tie(v.mCenter, v.mLod, v.mLodFlags);
    }

    inline bool operator<(const ChunkId& l, const ChunkId& r)
    {
        return tie(l) < tie(r);
    }

    inline bool operator<(const ChunkId& l, const TerrainChunkTemplateId& r)
    {
        return TerrainChunkTemplateId{ l.mCenter, l.mLod } < r;
    }

    inline bool operator<(const TerrainChunkTemplateId& l, const ChunkId& r)
    {
        return l < TerrainChunkTemplateId{ r.mCenter, r.mLod };
    }

    /// @brief Handles loading and caching of terrain chunks
    class ChunkManager : public Resource::GenericResourceManager<ChunkId>, public QuadTreeWorld::ChunkManager
    {
    public:
        ChunkManager(Storage* storage, Resource::SceneManager* sceneMgr, TextureManager* textureManager, CompositeMapRenderer* renderer);

        osg::ref_ptr<osg::Node> getChunk(float size, const osg::Vec2f& center, unsigned char lod, unsigned int lodFlags, bool activeGrid, const osg::Vec3f& viewPoint, bool compile) override;

        void setCompositeMapSize(unsigned int size) { mCompositeMapSize = size; }
        void setCompositeMapLevel(float level) { mCompositeMapLevel = level; }
        void setMaxCompositeGeometrySize(float maxCompGeometrySize) { mMaxCompGeometrySize = maxCompGeometrySize; }

        void updateTextureFiltering();

        void setNodeMask(unsigned int mask) { mNodeMask = mask; }
        unsigned int getNodeMask() override { return mNodeMask; }

        void reportStats(unsigned int frameNumber, osg::Stats* stats) const override;

        void clearCache() override;

        void releaseGLObjects(osg::State* state) override;

    private:
        osg::ref_ptr<osg::Node> createChunk(float size, const osg::Vec2f& center, unsigned char lod, unsigned int lodFlags, bool compile, const TerrainDrawable* templateGeometry);

        osg::ref_ptr<osg::Texture2D> createCompositeMapRTT();

        void createCompositeMapGeometry(float chunkSize, const osg::Vec2f& chunkCenter, const osg::Vec4f& texCoords, CompositeMap& map);

        std::vector<osg::ref_ptr<osg::StateSet> > createPasses(float chunkSize, const osg::Vec2f& chunkCenter, bool forCompositeMap);

        Terrain::Storage* mStorage;
        Resource::SceneManager* mSceneManager;
        TextureManager* mTextureManager;
        CompositeMapRenderer* mCompositeMapRenderer;
        BufferCache mBufferCache;

        osg::ref_ptr<osg::StateSet> mMultiPassRoot;

        unsigned int mNodeMask;

        unsigned int mCompositeMapSize;
        float mCompositeMapLevel;
        float mMaxCompGeometrySize;
    };

}

#endif
