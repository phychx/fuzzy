// todo: for defines and such - won't be needed in future
#include "glad/glad.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <iostream>

#include "fuzzy_types.h"
#include "fuzzy_platform.h"
#include "tiled.cpp"
#include "fuzzy.h"

global_variable const u32 FLIPPED_HORIZONTALLY_FLAG = 0x80000000;
global_variable const u32 FLIPPED_VERTICALLY_FLAG = 0x40000000;
global_variable const u32 FLIPPED_DIAGONALLY_FLAG = 0x20000000;

internal_function inline vec3
NormalizeRGB(u32 Red, u32 Green, u32 Blue)
{
    const f32 MAX = 255.f;
    return vec3(Red / MAX, Green / MAX, Blue / MAX);
}

internal_function u32
CreateShader(game_memory *Memory, game_state *GameState, GLenum Type, char *Source)
{
    u32 Shader = Memory->Renderer.glCreateShader(Type);
    Memory->Renderer.glShaderSource(Shader, 1, &Source, NULL);
    Memory->Renderer.glCompileShader(Shader);

    s32 IsShaderCompiled;
    Memory->Renderer.glGetShaderiv(Shader, GL_COMPILE_STATUS, &IsShaderCompiled);
    if (!IsShaderCompiled)
    {
        s32 LogLength;
        Memory->Renderer.glGetShaderiv(Shader, GL_INFO_LOG_LENGTH, &LogLength);

        // todo: use temporary memory
        char *ErrorLog = PushString(&GameState->WorldArena, LogLength);

        Memory->Renderer.glGetShaderInfoLog(Shader, LogLength, NULL, ErrorLog);

        char Output[1024];
        snprintf(Output, sizeof(Output), "%s%s%s", "Shader compilation failed:\n", ErrorLog, "\n");
        Memory->Platform.PrintOutput(Output);

        Memory->Renderer.glDeleteShader(Shader);
    }
    assert(IsShaderCompiled);

    return Shader;
}

internal_function u32
CreateProgram(game_memory *Memory, game_state *GameState, u32 VertexShader, u32 FragmentShader)
{
    u32 Program = Memory->Renderer.glCreateProgram();
    Memory->Renderer.glAttachShader(Program, VertexShader);
    Memory->Renderer.glAttachShader(Program, FragmentShader);
    Memory->Renderer.glLinkProgram(Program);
    Memory->Renderer.glDeleteShader(VertexShader);
    Memory->Renderer.glDeleteShader(FragmentShader);

    s32 IsProgramLinked;
    Memory->Renderer.glGetProgramiv(Program, GL_LINK_STATUS, &IsProgramLinked);

    if (!IsProgramLinked)
    {
        s32 LogLength;
        Memory->Renderer.glGetProgramiv(Program, GL_INFO_LOG_LENGTH, &LogLength);

        // todo: use temporary memory
        char *ErrorLog = PushString(&GameState->WorldArena, LogLength);

        Memory->Renderer.glGetProgramInfoLog(Program, LogLength, nullptr, ErrorLog);

        char Output[1024];
        snprintf(Output, sizeof(Output), "%s%s%s", "Shader program linkage failed:\n", ErrorLog, "\n");
        Memory->Platform.PrintOutput(Output);
    }
    assert(IsProgramLinked);

    return Program;
}

internal_function inline s32
GetUniformLocation(game_memory *Memory, u32 ShaderProgram, const char *Name)
{
    s32 UniformLocation = Memory->Renderer.glGetUniformLocation(ShaderProgram, Name);
    //assert(uniformLocation != -1);
    return UniformLocation;
}

internal_function inline void
SetShaderUniform(game_memory *Memory, s32 Location, s32 Value)
{
    Memory->Renderer.glUniform1i(Location, Value);
}

internal_function inline void
SetShaderUniform(game_memory *Memory, s32 Location, f32 Value)
{
    Memory->Renderer.glUniform1f(Location, Value);
}

internal_function inline void
SetShaderUniform(game_memory *Memory, s32 Location, vec2 Value)
{
    Memory->Renderer.glUniform2f(Location, Value.x, Value.y);
}

internal_function inline void
SetShaderUniform(game_memory *Memory, s32 Location, const mat4& Value)
{
    Memory->Renderer.glUniformMatrix4fv(Location, 1, GL_FALSE, glm::value_ptr(Value));
}

// todo: write more efficient functions
internal_function inline f32
Clamp(f32 Value, f32 Min, f32 Max)
{
    if (Value < Min) return Min;
    if (Value > Max) return Max;

    return Value;
}

//inline f32 abs(f32 value) {
//    if (value < 0.f) return -value;
//
//    return value;
//}

internal_function inline f32
GetRandomInRange(f32 Min, f32 Max)
{
    f32 Result = Min + (f32)(rand()) / ((f32)(RAND_MAX / (Max - Min)));
    return Result;
}

internal_function inline b32
IntersectAABB(const aabb& Box1, const aabb& Box2)
{
    // Separating Axis Theorem
    b32 XCollision = Box1.Position.x + Box1.Size.x > Box2.Position.x && Box1.Position.x < Box2.Position.x + Box2.Size.x;
    b32 YCollision = Box1.Position.y + Box1.Size.y > Box2.Position.y && Box1.Position.y < Box2.Position.y + Box2.Size.y;

    return XCollision && YCollision;
}

// todo: replace with smth more performant
//inline drawable_entity *
//GetEntityById(game_state *GameState, u32 Id) {
//    drawable_entity *Result = nullptr;
//
//    for (u32 DrawableEntityIndex = 0; DrawableEntityIndex < GameState->DrawableEntitiesCount; ++DrawableEntityIndex) {
//        drawable_entity *Entity = &GameState->DrawableEntities[DrawableEntityIndex];
//
//        if (Entity->Id == Id) {
//            Result = Entity;
//            break;
//        }
//    }
//
//    assert(Result);
//
//    return Result;
//}

internal_function u32
FindFirstUnusedParticle(particle_emitter *Emitter)
{
    for (u32 i = Emitter->LastUsedParticle; i < Emitter->ParticlesCount; ++i)
    {
        if (Emitter->Particles[i].Lifespan <= 0.f)
        {
            return i;
        }
    }

    for (u32 i = 0; i < Emitter->LastUsedParticle; ++i)
    {
        if (Emitter->Particles[i].Lifespan <= 0.f)
        {
            return i;
        }
    }

    return 0;       // all particles are taken, override the first one
}

// basic Minkowski-based collision detection
internal_function vec2
SweptAABB(const vec2 Point, const vec2 Delta, const aabb& Box, const vec2 Padding)
{
    vec2 Time = vec2(1.f);

    f32 LeftTime = 1.f;
    f32 RightTime = 1.f;
    f32 TopTime = 1.f;
    f32 BottomTime = 1.f;

    vec2 Position = Box.Position - Padding;
    vec2 Size = Box.Size + Padding;

    if (Delta.x != 0.f && Position.y < Point.y && Point.y < Position.y + Size.y)
    {
        LeftTime = (Position.x - Point.x) / Delta.x;
        if (LeftTime < Time.x)
        {
            Time.x = LeftTime;
        }

        RightTime = (Position.x + Size.x - Point.x) / Delta.x;
        if (RightTime < Time.x)
        {
            Time.x = RightTime;
        }
    }

    if (Delta.y != 0.f && Position.x < Point.x && Point.x < Position.x + Size.x)
    {
        TopTime = (Position.y - Point.y) / Delta.y;
        if (TopTime < Time.y)
        {
            Time.y = TopTime;
        }

        BottomTime = (Position.y + Size.y - Point.y) / Delta.y;
        if (BottomTime < Time.y)
        {
            Time.y = BottomTime;
        }
    }

    return Time;
}

global_variable const vec3 BackgroundColor = NormalizeRGB(29, 33, 45);

internal_function void
ProcessInput(game_state *GameState, game_input *Input)
{
    f32 Delta = 0.001f;

    if (Input->Left.isPressed)
    {
        GameState->Zoom -= Delta;
    }

    if (Input->Right.isPressed)
    {
        GameState->Zoom += Delta;
    }

    if (GameState->Zoom < 0.1f) {
        GameState->Zoom = 0.1f;
    }

    //sprite *Bob = &GameState->Bob;
    //sprite *Swoosh = &GameState->Swoosh;

    //if (Input->Keys[KEY_LEFT] == KEY_PRESS) {
    //    if (
    //        Bob->CurrentAnimation != Bob->Animations[2] &&
    //        Bob->CurrentAnimation != Bob->Animations[1] &&
    //        Bob->CurrentAnimation != Bob->Animations[3]
    //        ) {
    //        Bob->CurrentAnimation = Bob->Animations[2];
    //    }
    //    Bob->Acceleration.x = -12.f;
    //    Bob->Flipped |= FLIPPED_HORIZONTALLY_FLAG;
    //}

    //if (Input->Keys[KEY_RIGHT] == KEY_PRESS) {
    //    if (
    //        Bob->CurrentAnimation != Bob->Animations[2] &&
    //        Bob->CurrentAnimation != Bob->Animations[1] &&
    //        Bob->CurrentAnimation != Bob->Animations[3]
    //        ) {
    //        Bob->CurrentAnimation = Bob->Animations[2];
    //    }
    //    Bob->Acceleration.x = 12.f;
    //    Bob->Flipped &= 0;
    //}

    //if (Input->Keys[KEY_LEFT] == KEY_RELEASE && !Input->ProcessedKeys[KEY_LEFT]) {
    //    Input->ProcessedKeys[KEY_LEFT] = true;
    //    if (Bob->CurrentAnimation != Bob->Animations[0]) {
    //        Bob->CurrentAnimation = Bob->Animations[0];
    //        Bob->XAnimationOffset = 0.f;
    //        Bob->Flipped |= FLIPPED_HORIZONTALLY_FLAG;
    //    }
    //}

    //if (Input->Keys[KEY_RIGHT] == KEY_RELEASE && !Input->ProcessedKeys[KEY_RIGHT]) {
    //    Input->ProcessedKeys[KEY_RIGHT] = true;
    //    if (Bob->CurrentAnimation != Bob->Animations[0]) {
    //        Bob->CurrentAnimation = Bob->Animations[0];
    //        Bob->XAnimationOffset = 0.f;
    //        Bob->Flipped &= 0;
    //    }
    //}

    //if (Input->Keys[KEY_SPACE] == KEY_PRESS && !Input->ProcessedKeys[KEY_SPACE]) {
    //    Input->ProcessedKeys[KEY_SPACE] = true;
    //    Bob->Acceleration.y = -350.f;
    //    Bob->Velocity.y = 0.f;
    //}

    //if (Input->Keys[KEY_S] == KEY_PRESS && !Input->ProcessedKeys[KEY_S]) {
    //    Input->ProcessedKeys[KEY_S] = true;
    //    Bob->CurrentAnimation = Bob->Animations[3];
    //    Bob->XAnimationOffset = 0.f;

    //    Swoosh->ShouldRender = true;
    //    Swoosh->XAnimationOffset = 0.f;
    //    Swoosh->Flipped = Bob->Flipped;

    //    // todo: make it better
    //    for (u32 DrawableEntityIndex = 0; DrawableEntityIndex < GameState->DrawableEntitiesCount; ++DrawableEntityIndex) {
    //        drawable_entity *Entity = &GameState->DrawableEntities[DrawableEntityIndex];
    //        if (Entity->Type == tile_type::REFLECTOR) {
    //            Entity->UnderEffect = false;
    //        }
    //    }

    //    if (Swoosh->Flipped & FLIPPED_HORIZONTALLY_FLAG) {
    //        Swoosh->Position = { Bob->Box.Position.x - 2 * TILE_SIZE.x, Bob->Box.Position.y };
    //        Swoosh->Box.Position = { Bob->Box.Position.x - 2 * TILE_SIZE.x, Bob->Box.Position.y };
    //    }
    //    else {
    //        Swoosh->Position = { Bob->Box.Position.x + TILE_SIZE.x, Bob->Box.Position.y };
    //        Swoosh->Box.Position = { Bob->Box.Position.x + TILE_SIZE.x, Bob->Box.Position.y };
    //    }
    //}
}

//inline void
//AssignAnimationsToEntity(game_state *GameState, json *AnimationsConfig, u32 Index, sprite *Entity) {
//    auto EntityConfig = (*AnimationsConfig)["sprites"][Index];
//    auto EntityAnimations = EntityConfig["animations"];
//
//    Entity->AnimationsCount = (u32)EntityAnimations.size();
//    Entity->Animations = PushArray<animation>(&GameState->WorldArena, Entity->AnimationsCount);
//
//    u32 EntityAnimationsIndex = 0;
//    for (auto Animation : EntityAnimations) {
//        Entity->Animations[EntityAnimationsIndex] = {
//            Animation["x"], Animation["y"], Animation["frames"], Animation["delay"], Animation["size"]
//        };
//
//        string AnimationDirection = Animation["direction"];
//        if (AnimationDirection == "right") {
//            Entity->Animations[EntityAnimationsIndex].Direction = direction::RIGHT;
//        }
//        else if (AnimationDirection == "left") {
//            Entity->Animations[EntityAnimationsIndex].Direction = direction::LEFT;
//        }
//
//        ++EntityAnimationsIndex;
//    }
//
//    Entity->CurrentAnimation = Entity->Animations[0];
//    Entity->XAnimationOffset = 0.f;
//    Entity->FrameTime = 0.f;
//}

extern "C" EXPORT GAME_UPDATE_AND_RENDER(GameUpdateAndRender)
{
    assert(sizeof(game_state) <= Memory->PermanentStorageSize);

    game_state *GameState = (game_state*)Memory->PermanentStorage;

    s32 ScreenWidth = Params->ScreenWidth;
    s32 ScreenHeight = Params->ScreenHeight;
    vec2 ScreenCenter = vec2(ScreenWidth / 2.f, ScreenHeight / 2.f);

    platform_api *Platform = &Memory->Platform;
    renderer_api *Renderer = &Memory->Renderer;

    if (!GameState->IsInitialized)
    {
        InitializeMemoryArena(
            &GameState->WorldArena,
            Memory->PermanentStorageSize - sizeof(game_state),
            (u8*)Memory->PermanentStorage + sizeof(game_state)
        );

        GameState->ScreenWidthInMeters = 20.f;
        f32 MetersToPixels = (f32)ScreenWidth / GameState->ScreenWidthInMeters;
        f32 PixelsToMeters = 1.f / MetersToPixels;
        GameState->ScreenHeightInMeters = ScreenHeight * PixelsToMeters;

        //string TilesetJson = Platform->ReadTextFile("tilesets/tileset.json");
        //GameState->Tileset = {};
        //LoadTileset(&GameState->Tileset, TilesetJson.c_str(), &GameState->WorldArena, Platform);
        //tile_meta_info* TileInfo = GetTileMetaInfo(&GameState->Tileset, 512);

        string MapJson = Platform->ReadTextFile("maps/map01.json");
        GameState->Map = {};
        LoadMap(&GameState->Map, MapJson.c_str(), &GameState->WorldArena, Platform);

        tile_meta_info *TileInfo = GetTileMetaInfo(&GameState->Map.Tilesets[0].Source, 544);

        // todo: eventually support multiple tilesets
        tileset *Tileset = &GameState->Map.Tilesets[0].Source;
        u32 TilesetFirstGID = GameState->Map.Tilesets[0].FirstGID;

        u32 texture;
        Renderer->glGenTextures(1, &texture);
        Renderer->glBindTexture(GL_TEXTURE_2D, texture);

        // note: default value for GL_TEXTURE_MIN_FILTER is GL_NEAREST_MIPMAP_LINEAR
        // since we do not use mipmaps we must override this value
        Renderer->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        Renderer->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        Renderer->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Tileset->Image.Width, Tileset->Image.Height,
            0, GL_RGBA, GL_UNSIGNED_BYTE, Tileset->Image.Memory);

        //Platform->FreeImageFile(textureImage);
        vec2 TileSize01 = vec2((f32)Tileset->TileWidthInPixels / (f32)Tileset->Image.Width,
            (f32)Tileset->TileHeightInPixels / (f32)Tileset->Image.Height);

        {
            string VertexShaderSource = Memory->Platform.ReadTextFile("shaders/tile.vert");
            string FragmentShaderSource = Memory->Platform.ReadTextFile("shaders/tile.frag");
            u32 VertexShader = CreateShader(Memory, GameState, GL_VERTEX_SHADER, &VertexShaderSource[0]);
            u32 FragmentShader = CreateShader(Memory, GameState, GL_FRAGMENT_SHADER, &FragmentShaderSource[0]);
            GameState->TilesShaderProgram = CreateProgram(Memory, GameState, VertexShader, FragmentShader);

            Renderer->glUseProgram(GameState->TilesShaderProgram);

            GameState->VPUniformLocation = GetUniformLocation(Memory, GameState->TilesShaderProgram, "u_VP");

            s32 TileSizeUniformLocation = GetUniformLocation(Memory, GameState->TilesShaderProgram, "u_TileSize");
            SetShaderUniform(Memory, TileSizeUniformLocation, TileSize01);
        }

        {
            string VertexShaderSource = Memory->Platform.ReadTextFile("shaders/box.vert");
            string FragmentShaderSource = Memory->Platform.ReadTextFile("shaders/box.frag");
            u32 VertexShader = CreateShader(Memory, GameState, GL_VERTEX_SHADER, &VertexShaderSource[0]);
            u32 FragmentShader = CreateShader(Memory, GameState, GL_FRAGMENT_SHADER, &FragmentShaderSource[0]);
            GameState->TileBoxesShaderProgram = CreateProgram(Memory, GameState, VertexShader, FragmentShader);

            Renderer->glUseProgram(GameState->TileBoxesShaderProgram);

            GameState->VPUniformLocation2 = GetUniformLocation(Memory, GameState->TileBoxesShaderProgram, "u_VP");
        }

        //json AnimationsConfig = Platform->ReadJsonFile("animations.json");

        //*Bob = {};
        //Bob->Position = { 5 * TILE_SIZE.x, 0 * TILE_SIZE.y };
        //Bob->Box.Position = { 5 * TILE_SIZE.x, 0 * TILE_SIZE.y };
        //Bob->Box.Size = { 13.f * Scale.x, 16.f * Scale.y };
        //Bob->Velocity = { 0.f, 0.f };
        //Bob->Acceleration = { 0.f, 10.f };

        //AssignAnimationsToEntity(GameState, &AnimationsConfig, 0, Bob);

        //*Swoosh = {};
        //Swoosh->Position = { 0.f, 0.f };     // todo: think about better ways
        //Swoosh->Box.Position = { 0.f, 0.f };     // todo: think about better ways
        //Swoosh->Box.Size = { 2 * TILE_SIZE.x, TILE_SIZE.y };
        //Swoosh->ShouldRender = false;

        //AssignAnimationsToEntity(GameState, &AnimationsConfig, 1, Swoosh);

        //GameState->Lamp = {};
        //AssignAnimationsToEntity(GameState, &AnimationsConfig, 2, &GameState->Lamp);

        //GameState->Platform = {};
        //AssignAnimationsToEntity(GameState, &AnimationsConfig, 3, &GameState->Platform);

        //*Enemy = {};
        //Enemy->Position = { 13 * TILE_SIZE.x, 10 * TILE_SIZE.y };
        //Enemy->Box.Position = { 13 * TILE_SIZE.x, 10 * TILE_SIZE.y };
        //Enemy->Box.Size = { TILE_SIZE.x, TILE_SIZE.y };
        //Enemy->Velocity = { 0.f, 0.f };
        //Enemy->Acceleration = { 0.f, 10.f };

        //AssignAnimationsToEntity(GameState, &AnimationsConfig, 4, Enemy);


        //GameState->SpriteWidth = ((f32)Tileset->TileSize.x) / Tileset->Image.Width;
        //GameState->SpriteHeight = ((f32)Tileset->TileSize.y) / Tileset->Image.Height;

        //s32 spriteSizeUniformLocation = getUniformLocation(Memory, Program, "spriteSize");
        //setShaderUniform(Memory, spriteSizeUniformLocation, vec2(GameState->SpriteWidth, GameState->SpriteHeight));

        f32 QuadVertices[] = {
            // Pos     // UV
            0.f, 0.f,  0.f, 1.f,
            0.f, 1.f,  0.f, 0.f,
            1.f, 0.f,  1.f, 1.f,
            1.f, 1.f,  1.f, 0.f
        };

        //GameState->Map = LoadMap(&GameState->WorldArena, Platform->ReadJsonFile, "maps/map01.json", GameState->Tileset, Scale);

        // todo: make it efficient
        //for (u32 TileLayersIndex = 0; TileLayersIndex < GameState->Map.TileLayerCount; ++TileLayersIndex) {
        //    GameState->TilesCount += GameState->Map.TileLayers[TileLayersIndex].TilesCount;
        //}

        //GameState->Tiles = PushArray<tile>(&GameState->WorldArena, GameState->TilesCount);

        //u32 TotalTilesIndex = 0;
        //for (u32 TileLayersIndex = 0; TileLayersIndex < GameState->Map.TileLayerCount; ++TileLayersIndex) {
        //    for (u32 TilesIndex = 0; TilesIndex < GameState->Map.TileLayers[TileLayersIndex].TilesCount; ++TilesIndex) {
        //        GameState->Tiles[TotalTilesIndex++] = GameState->Map.TileLayers[TileLayersIndex].Tiles[TilesIndex];
        //    }
        //}

        //u64 TotalTileSizeInBytes = GameState->TilesCount * sizeof(tile);

        // todo: in future all these counts will be in asset pack file 
        GameState->TotalTileCount = 0;
        GameState->TotalTileBoxCount = 0;
        for (u32 TileLayerIndex = 0; TileLayerIndex < GameState->Map.TileLayerCount; ++TileLayerIndex)
        {
            for (u32 ChunkIndex = 0; ChunkIndex < GameState->Map.TileLayers[TileLayerIndex].ChunkCount; ++ChunkIndex)
            {
                for (u32 GIDIndex = 0; GIDIndex < GameState->Map.TileLayers[TileLayerIndex].Chunks[ChunkIndex].GIDCount; ++GIDIndex)
                {
                    u32 GID = GameState->Map.TileLayers[TileLayerIndex].Chunks[ChunkIndex].GIDs[GIDIndex];
                    if (GID > 0)
                    {
                        ++GameState->TotalTileCount;

                        tile_meta_info *TileInfo = GetTileMetaInfo(Tileset, GID - TilesetFirstGID);
                        if (TileInfo)
                        {
                            GameState->TotalTileBoxCount += TileInfo->BoxCount;
                        }
                    }
                }
            }
        }

        mat4 *TileInstanceModels = PushArray<mat4>(&GameState->WorldArena, GameState->TotalTileCount);
        vec2 *TileInstanceUVOffsets01 = PushArray<vec2>(&GameState->WorldArena, GameState->TotalTileCount);
        
        aabb *TileBoxes = PushArray<aabb>(&GameState->WorldArena, GameState->TotalTileBoxCount);
        mat4 *TileBoxInstanceModels = PushArray<mat4>(&GameState->WorldArena, GameState->TotalTileBoxCount);

        vec2 ScreenCenterInMeters = vec2(
            GameState->ScreenWidthInMeters / 2.f,
            GameState->ScreenHeightInMeters / 2.f
        );

        u32 TileInstanceIndex = 0;
        u32 TileBoxIndex = 0;
        for (u32 TileLayerIndex = 0; TileLayerIndex < GameState->Map.TileLayerCount; ++TileLayerIndex)
        {
            for (u32 ChunkIndex = 0; ChunkIndex < GameState->Map.TileLayers[TileLayerIndex].ChunkCount; ++ChunkIndex)
            {
                for (u32 GIDIndex = 0; GIDIndex < GameState->Map.TileLayers[TileLayerIndex].Chunks[ChunkIndex].GIDCount; ++GIDIndex)
                {
                    map_chunk *Chunk = GameState->Map.TileLayers[TileLayerIndex].Chunks + ChunkIndex;
                    u32 GID = Chunk->GIDs[GIDIndex];
                    if (GID > 0)
                    {
                        // TileInstanceModel
                        mat4 *TileInstanceModel = TileInstanceModels + TileInstanceIndex;
                        *TileInstanceModel = mat4(1.f);

                        s32 TileMapX = Chunk->X + (GIDIndex % Chunk->Width);
                        s32 TileMapY = Chunk->Y + (GIDIndex / Chunk->Height);

                        f32 TileXMeters = ScreenCenterInMeters.x + TileMapX * Tileset->TileWidthInMeters;
                        f32 TileYMeters = ScreenCenterInMeters.y - TileMapY * Tileset->TileHeightInMeters;

                        *TileInstanceModel = glm::translate(*TileInstanceModel, vec3(TileXMeters, TileYMeters, 0.f));
                        *TileInstanceModel = glm::scale(*TileInstanceModel, 
                            vec3(Tileset->TileWidthInMeters, Tileset->TileHeightInMeters, 0.f));

                        // TileInstanceUVOffset01
                        vec2 *TileInstanceUVOffset01 = TileInstanceUVOffsets01 + TileInstanceIndex;

                        s32 TileX = (GID - TilesetFirstGID) % Tileset->Columns;
                        s32 TileY = (GID - TilesetFirstGID) / Tileset->Columns;

                        *TileInstanceUVOffset01 = vec2(
                            (TileX * (Tileset->TileWidthInPixels + Tileset->Spacing) + Tileset->Margin) / Tileset->Image.Width,
                            (TileY * (Tileset->TileHeightInPixels + Tileset->Spacing) + Tileset->Margin) / Tileset->Image.Height
                        );

                        *TileInstanceUVOffset01 = vec2(TileX * TileSize01.x, TileY * TileSize01.y);

                        tile_meta_info *TileInfo = GetTileMetaInfo(Tileset, GID - TilesetFirstGID);
                        if (TileInfo)
                        {
                            // TileBox
                            for (u32 CurrentTileBoxIndex = 0; CurrentTileBoxIndex < TileInfo->BoxCount; ++CurrentTileBoxIndex)
                            {
                                aabb *TileBox = TileBoxes + TileBoxIndex;

                                f32 TilesetPixelsToMeters = Tileset->TileWidthInMeters / Tileset->TileWidthInPixels;

                                TileBox->Position.x = TileXMeters + 
                                    TileInfo->Boxes[CurrentTileBoxIndex].Position.x * TilesetPixelsToMeters;
                                TileBox->Position.y = TileYMeters + 
                                    ((Tileset->TileHeightInPixels - 
                                        TileInfo->Boxes[CurrentTileBoxIndex].Position.y - 
                                        TileInfo->Boxes[CurrentTileBoxIndex].Size.y) * TilesetPixelsToMeters);

                                TileBox->Size.x = TileInfo->Boxes[CurrentTileBoxIndex].Size.x * TilesetPixelsToMeters;
                                TileBox->Size.y = TileInfo->Boxes[CurrentTileBoxIndex].Size.y * TilesetPixelsToMeters;

                                mat4 *TileBoxInstanceModel = TileBoxInstanceModels + TileBoxIndex;
                                *TileBoxInstanceModel = mat4(1.f);

                                *TileBoxInstanceModel = glm::translate(*TileBoxInstanceModel, 
                                    vec3(TileBox->Position.x, TileBox->Position.y, 0.f));
                                *TileBoxInstanceModel = glm::scale(*TileBoxInstanceModel,
                                    vec3(TileBox->Size.x, TileBox->Size.y, 0.f));

                                ++TileBoxIndex;
                            }
                        }

                        ++TileInstanceIndex;
                    }
                }
            }
        }
        /*
        Chunk-based rendering.

        Divide your map into chunks.
        (Small sized squares of tiles; something like 32x32 tiles would work.)
        Make a separate VBO (possibly IBO) for each chunk.
        Only draw visible chunks.
        When a chunk is modified, update it's VBO accordingly.
        If you want an infinite map, you'll have to create and destroy chunks on the fly. Otherwise it shouldn't be necessary.
        */

        // Tiles
        Renderer->glGenVertexArrays(1, &GameState->TilesVAO);
        Renderer->glBindVertexArray(GameState->TilesVAO);

        u32 TilesVBO;
        Renderer->glGenBuffers(1, &TilesVBO);
        Renderer->glBindBuffer(GL_ARRAY_BUFFER, TilesVBO);
        Renderer->glBufferData(GL_ARRAY_BUFFER, sizeof(QuadVertices) + GameState->TotalTileCount * (sizeof(mat4) + sizeof(vec2)),
            NULL, GL_STATIC_DRAW);

        Renderer->glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(QuadVertices), QuadVertices);
        Renderer->glBufferSubData(GL_ARRAY_BUFFER, sizeof(QuadVertices),
            GameState->TotalTileCount * sizeof(mat4), TileInstanceModels);
        Renderer->glBufferSubData(GL_ARRAY_BUFFER, sizeof(QuadVertices) + GameState->TotalTileCount * sizeof(mat4),
            GameState->TotalTileCount * sizeof(vec2), TileInstanceUVOffsets01);

        Renderer->glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(vec4), (void *)0);
        Renderer->glEnableVertexAttribArray(0);

        Renderer->glEnableVertexAttribArray(1);
        Renderer->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(mat4),
            (void *)(sizeof(QuadVertices)));
        Renderer->glVertexAttribDivisor(1, 1);

        Renderer->glEnableVertexAttribArray(2);
        Renderer->glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(mat4),
            (void *)(sizeof(QuadVertices) + sizeof(vec4)));
        Renderer->glVertexAttribDivisor(2, 1);

        Renderer->glEnableVertexAttribArray(3);
        Renderer->glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(mat4),
            (void *)(sizeof(QuadVertices) + 2 * sizeof(vec4)));
        Renderer->glVertexAttribDivisor(3, 1);

        Renderer->glEnableVertexAttribArray(4);
        Renderer->glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(mat4),
            (void *)(sizeof(QuadVertices) + 3 * sizeof(vec4)));
        Renderer->glVertexAttribDivisor(4, 1);

        Renderer->glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(vec2),
            (void *)(sizeof(QuadVertices) + GameState->TotalTileCount * sizeof(mat4)));
        Renderer->glEnableVertexAttribArray(5);
        Renderer->glVertexAttribDivisor(5, 1);


        // Tile Boxes
        Renderer->glGenVertexArrays(1, &GameState->TileBoxesVAO);
        Renderer->glBindVertexArray(GameState->TileBoxesVAO);

        u32 TileBoxesVBO;
        Renderer->glGenBuffers(1, &TileBoxesVBO);
        Renderer->glBindBuffer(GL_ARRAY_BUFFER, TileBoxesVBO);
        Renderer->glBufferData(GL_ARRAY_BUFFER, sizeof(QuadVertices) + GameState->TotalTileBoxCount * sizeof(mat4),
            NULL, GL_STATIC_DRAW);

        Renderer->glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(QuadVertices), QuadVertices);
        Renderer->glBufferSubData(GL_ARRAY_BUFFER, sizeof(QuadVertices),
            GameState->TotalTileBoxCount * sizeof(mat4), TileBoxInstanceModels);

        Renderer->glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(vec4), (void *)0);
        Renderer->glEnableVertexAttribArray(0);

        Renderer->glEnableVertexAttribArray(1);
        Renderer->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(mat4),
            (void *)(sizeof(QuadVertices)));
        Renderer->glVertexAttribDivisor(1, 1);

        Renderer->glEnableVertexAttribArray(2);
        Renderer->glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(mat4),
            (void *)(sizeof(QuadVertices) + sizeof(vec4)));
        Renderer->glVertexAttribDivisor(2, 1);

        Renderer->glEnableVertexAttribArray(3);
        Renderer->glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(mat4),
            (void *)(sizeof(QuadVertices) + 2 * sizeof(vec4)));
        Renderer->glVertexAttribDivisor(3, 1);

        Renderer->glEnableVertexAttribArray(4);
        Renderer->glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(mat4),
            (void *)(sizeof(QuadVertices) + 3 * sizeof(vec4)));
        Renderer->glVertexAttribDivisor(4, 1);

        //Renderer->glEnableVertexAttribArray(1);
        //Renderer->glVertexAttribDivisor(1, 1);

        //Renderer->glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        //Renderer->glBufferSubData(GL_ARRAY_BUFFER, sizeof(vertices), TotalTileSizeInBytes, GameState->Tiles);

        // vertices
        //Renderer->glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(f32), (void*)0);
        //Renderer->glEnableVertexAttribArray(0);

        //// tile Position/uv
        //Renderer->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(tile), (void*) sizeof(vertices));
        //Renderer->glEnableVertexAttribArray(1);
        //Renderer->glVertexAttribDivisor(1, 1);

        //// tile flipped
        //Renderer->glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, sizeof(tile), (void*)(sizeof(vertices) + Offset(tile, Flipped)));
        //Renderer->glEnableVertexAttribArray(2);
        //Renderer->glVertexAttribDivisor(2, 1);

        // todo: make it efficient
        //for (u32 ObjectLayersIndex = 0; ObjectLayersIndex < GameState->Map.ObjectLayerCount; ++ObjectLayersIndex) {
        //    GameState->EntitiesCount += GameState->Map.ObjectLayers[ObjectLayersIndex].EntitiesCount;
        //    GameState->DrawableEntitiesCount += GameState->Map.ObjectLayers[ObjectLayersIndex].DrawableEntitiesCount;
        //}
        //
        //GameState->Entities = PushArray<entity>(&GameState->WorldArena, GameState->EntitiesCount);

        //// + player and swoosh effect
        //GameState->DrawableEntitiesCount += 2;
        //GameState->DrawableEntities = PushArray<drawable_entity>(&GameState->WorldArena, GameState->DrawableEntitiesCount);

        //u32 TotalEntityIndex = 0;
        //u32 TotalDrawableEntityIndex = 0;
        //for (u32 ObjectLayersIndex = 0; ObjectLayersIndex < GameState->Map.ObjectLayerCount; ++ObjectLayersIndex) {
        //    for (u32 EntityIndex = 0; EntityIndex < GameState->Map.ObjectLayers[ObjectLayersIndex].EntitiesCount; ++EntityIndex) {
        //        GameState->Entities[TotalEntityIndex++] = GameState->Map.ObjectLayers[ObjectLayersIndex].Entities[EntityIndex];
        //    }
        //    for (u32 DrawableEntityIndex = 0; DrawableEntityIndex < GameState->Map.ObjectLayers[ObjectLayersIndex].DrawableEntitiesCount; ++DrawableEntityIndex) {
        //        GameState->DrawableEntities[TotalDrawableEntityIndex++] = GameState->Map.ObjectLayers[ObjectLayersIndex].DrawableEntities[DrawableEntityIndex];
        //    }
        //}

        //GameState->Player = {};
        //GameState->Player.Position = Bob->Position;
        //GameState->Player.Box = Bob->Box;
        //GameState->Player.SpriteScale = vec2(1.f);
        //GameState->Player.Offset = (GameState->DrawableEntitiesCount - 2) * sizeof(drawable_entity);
        //GameState->Player.ShouldRender = 1;
        //GameState->Player.Collides = true;
        //GameState->Player.Type = tile_type::PLAYER;
        //GameState->DrawableEntities[GameState->DrawableEntitiesCount - 2] = GameState->Player;

        //GameState->SwooshEffect = {};
        //GameState->SwooshEffect.Position = Swoosh->Position;
        //GameState->SwooshEffect.Box = Swoosh->Box;
        //GameState->SwooshEffect.SpriteScale = vec2(2.f, 1.f);
        //GameState->SwooshEffect.Offset = (GameState->DrawableEntitiesCount - 1) * sizeof(drawable_entity);
        //GameState->SwooshEffect.ShouldRender = 0;
        //GameState->SwooshEffect.Collides = true;
        //GameState->SwooshEffect.Type = tile_type::EFFECT;
        //GameState->DrawableEntities[GameState->DrawableEntitiesCount - 1] = GameState->SwooshEffect;

        //Renderer->glGenBuffers(1, &GameState->VBOEntities);
        //Renderer->glBindBuffer(GL_ARRAY_BUFFER, GameState->VBOEntities);
        //Renderer->glBufferData(GL_ARRAY_BUFFER, (u32)(sizeof(u32) + sizeof(drawable_entity)) * GameState->DrawableEntitiesCount, GameState->DrawableEntities, GL_STREAM_DRAW);

        // Position
        //Renderer->glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(drawable_entity), (void*)Offset(drawable_entity, Position));
        //Renderer->glEnableVertexAttribArray(3);
        //Renderer->glVertexAttribDivisor(3, 1);
        //// aabb
        ////glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(drawable_entity), (void*) offset(drawable_entity, box));
        ////glEnableVertexAttribArray(4);
        ////glVertexAttribDivisor(4, 1);
        //// uv/rotation
        //Renderer->glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(drawable_entity), (void*)Offset(drawable_entity, UV));
        //Renderer->glEnableVertexAttribArray(5);
        //Renderer->glVertexAttribDivisor(5, 1);
        //// flipped
        //Renderer->glVertexAttribIPointer(6, 1, GL_UNSIGNED_INT, sizeof(drawable_entity), (void*)Offset(drawable_entity, Flipped));
        //Renderer->glEnableVertexAttribArray(6);
        //Renderer->glVertexAttribDivisor(6, 1);
        //// spriteScale
        //Renderer->glVertexAttribPointer(7, 2, GL_FLOAT, GL_FALSE, sizeof(drawable_entity), (void*)Offset(drawable_entity, SpriteScale));
        //Renderer->glEnableVertexAttribArray(7);
        //Renderer->glVertexAttribDivisor(7, 1);
        //// shouldRender
        //Renderer->glVertexAttribIPointer(8, 1, GL_UNSIGNED_INT, sizeof(drawable_entity), (void*)Offset(drawable_entity, ShouldRender));
        //Renderer->glEnableVertexAttribArray(8);
        //Renderer->glVertexAttribDivisor(8, 1);

        //GameState->ParticleEmittersIndex = 0;
        //GameState->ParticleEmittersMaxCount = 50;
        //GameState->ParticleEmitters = PushArray<particle_emitter>(&GameState->WorldArena, GameState->ParticleEmittersMaxCount);

        //particle_emitter Charge = {};
        //Charge.ParticlesCount = 500;
        //Charge.NewParticlesCount = 5;
        //Charge.Dt = 0.01f;
        //Charge.Position = { 4.5 * TILE_SIZE.x, 6.5 * TILE_SIZE.y };
        //Charge.Box.Position = { 4.5 * TILE_SIZE.x, 6.5 * TILE_SIZE.y };
        //Charge.Box.Size = { 0.1f * TILE_SIZE.x, 0.1f * TILE_SIZE.x };
        //Charge.Velocity = { 0.f, 0.f };
        //Charge.ReflectorIndex = -1;
        //Charge.TimeLeft = 3.f;
        //Charge.Particles = PushArray<particle>(&GameState->WorldArena, Charge.ParticlesCount);

        //GameState->ParticleEmitters[GameState->ParticleEmittersIndex++] = Charge;

        //Renderer->glGenBuffers(1, &GameState->VBOParticles);
        //Renderer->glBindBuffer(GL_ARRAY_BUFFER, GameState->VBOParticles);
        //// todo: manage it somehow
        //Renderer->glBufferData(GL_ARRAY_BUFFER, GameState->ParticleEmittersMaxCount * (Charge.ParticlesCount) * sizeof(particle),
        //    nullptr, GL_STREAM_DRAW);

        //// particle's Position/Size
        //Renderer->glVertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, sizeof(particle), (void*)Offset(particle, Position));
        //Renderer->glEnableVertexAttribArray(9);
        //Renderer->glVertexAttribDivisor(9, 1);
        //// particle's uv
        //Renderer->glVertexAttribPointer(10, 2, GL_FLOAT, GL_FALSE, sizeof(particle), (void*)Offset(particle, UV));
        //Renderer->glEnableVertexAttribArray(10);
        //Renderer->glVertexAttribDivisor(10, 1);
        //// particle's alpha value
        //Renderer->glVertexAttribPointer(11, 1, GL_FLOAT, GL_FALSE, sizeof(particle), (void*)Offset(particle, Alpha));
        //Renderer->glEnableVertexAttribArray(11);
        //Renderer->glVertexAttribDivisor(11, 1);

        GameState->UpdateRate = 0.01f;   // 10 ms
        GameState->Lag = 0.f;

        // todo: draw collision regions
        //Renderer->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        //GameState->ChargeSpawnCooldown = 0.f;

        Renderer->glClearColor(BackgroundColor.r, BackgroundColor.g, BackgroundColor.b, 1.f);

        GameState->IsInitialized = true;

        GameState->CameraPosition.x = 1.f;

        GameState->Zoom = 1.f / 1.f;
    }

    //tileset *Tileset = &GameState->Map.Tilesets[0].Source;

    //GameState->Lag += Params->Delta;

    //Renderer->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ProcessInput(GameState, &Params->Input);

    Renderer->glClear(GL_COLOR_BUFFER_BIT);

    GameState->Projection = glm::ortho(
        -GameState->ScreenWidthInMeters / 2.f * GameState->Zoom, GameState->ScreenWidthInMeters / 2.f * GameState->Zoom,
        -GameState->ScreenHeightInMeters / 2.f * GameState->Zoom, GameState->ScreenHeightInMeters / 2.f * GameState->Zoom
    );

    mat4 View = mat4(1.f);
    View = glm::translate(View, vec3(-GameState->ScreenWidthInMeters / 2.f, -GameState->ScreenHeightInMeters / 2.f, 0.f));

    mat4 VP = GameState->Projection * View;

    Renderer->glUseProgram(GameState->TilesShaderProgram);
    Renderer->glBindVertexArray(GameState->TilesVAO);

    SetShaderUniform(Memory, GameState->VPUniformLocation, VP);

    Renderer->glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, GameState->TotalTileCount);

    Renderer->glUseProgram(GameState->TileBoxesShaderProgram);
    Renderer->glBindVertexArray(GameState->TileBoxesVAO);

    SetShaderUniform(Memory, GameState->VPUniformLocation2, VP);

    Renderer->glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, GameState->TotalTileBoxCount);

    //while (GameState->Lag >= GameState->UpdateRate) {
    //    Renderer->glBindBuffer(GL_ARRAY_BUFFER, GameState->VBOEntities);

    //    f32 dt = 0.15f;

    //    Bob->Acceleration.x = 0.f;

    //    // friction imitation
    //    // todo: take scale into account!
    //    Bob->Acceleration.x += -0.5f * Bob->Velocity.x;
    //    Bob->Velocity.x += Bob->Acceleration.x * dt;

    //    Bob->Acceleration.y += -0.01f * Bob->Velocity.y;
    //    Bob->Velocity.y += Bob->Acceleration.y * dt;

    //    vec2 move = 0.5f * Bob->Acceleration * dt * dt + Bob->Velocity * dt;

    //    vec2 oldPosition = Bob->Position;
    //    vec2 time = vec2(1.f);

    //    for (u32 EntityIndex = 0; EntityIndex < GameState->EntitiesCount; ++EntityIndex) {
    //        vec2 t = sweptAABB(oldPosition, move, GameState->Entities[EntityIndex].Box, Bob->Box.Size);

    //        if (t.x >= 0.f && t.x < time.x) time.x = t.x;
    //        if (t.y >= 0.f && t.y < time.y) time.y = t.y;
    //    }

    //    for (u32 DrawableEntityIndex = 0; DrawableEntityIndex < GameState->DrawableEntitiesCount; ++DrawableEntityIndex) {
    //        drawable_entity *Entity = &GameState->DrawableEntities[DrawableEntityIndex];

    //        if (Entity->Type == tile_type::REFLECTOR || Entity->Type == tile_type::PLATFORM) {
    //            if (!Entity->Collides) break;

    //            vec2 t = sweptAABB(oldPosition, move, Entity->Box, Bob->Box.Size);

    //            if (t.x >= 0.f && t.x < time.x) time.x = t.x;
    //            if (t.y >= 0.f && t.y < time.y) time.y = t.y;

    //            if (Entity->Type == tile_type::REFLECTOR) {
    //                b32 swooshCollide = intersectAABB(Swoosh->Box, Entity->Box);
    //                if (!Entity->UnderEffect && swooshCollide) {
    //                    Entity->UnderEffect = true;
    //                    Entity->IsRotating = true;
    //                }

    //                if (Entity->IsRotating) {
    //                    Entity->Rotation += 5.f;
    //                    Renderer->glBufferSubData(GL_ARRAY_BUFFER, Entity->Offset + Offset(drawable_entity, Rotation), sizeof(u32), &Entity->Rotation);

    //                    if (0.f < Entity->Rotation && Entity->Rotation <= 90.f) {
    //                        if (Entity->Rotation == 90.f) {
    //                            Entity->IsRotating = false;
    //                            break;
    //                        }
    //                    }
    //                    if (90.f < Entity->Rotation && Entity->Rotation <= 180.f) {
    //                        if (Entity->Rotation == 180.f) {
    //                            Entity->IsRotating = false;
    //                            break;
    //                        }
    //                    }
    //                    if (180.f < Entity->Rotation && Entity->Rotation <= 270.f) {
    //                        if (Entity->Rotation == 270.f) {
    //                            Entity->IsRotating = false;
    //                            break;
    //                        }
    //                    }
    //                    if (270.f < Entity->Rotation && Entity->Rotation <= 360.f) {
    //                        if (Entity->Rotation == 360.f) {
    //                            Entity->IsRotating = false;
    //                            Entity->Rotation = 0.f;
    //                            break;
    //                        }
    //                    }
    //                }
    //            }
    //        }
    //    }

    //    if (time.x < 1.f) {
    //        Bob->Velocity.x = 0.f;
    //    }
    //    if (time.y < 1.f) {
    //        Bob->Velocity.y = 0.f;

    //        if (time.y > 0.f && move.y > 0.f && Bob->CurrentAnimation != Bob->Animations[1]) {
    //            Bob->CurrentAnimation = Bob->Animations[1];
    //            Bob->XAnimationOffset = 0.f;
    //        }
    //    }
    //    if (time.y == 1.f) {
    //        if (Bob->Velocity.y > 0.f) {
    //            if (Bob->CurrentAnimation != Bob->Animations[3]) {
    //                Bob->CurrentAnimation = Bob->Animations[5];
    //                Bob->XAnimationOffset = 0.f;
    //            }
    //        }
    //        else {
    //            if (Bob->CurrentAnimation != Bob->Animations[3]) {
    //                Bob->CurrentAnimation = Bob->Animations[4];
    //                Bob->XAnimationOffset = 0.f;
    //            }
    //        }
    //    }

    //    vec2 updatedMove = move * time;

    //    Bob->Position.x = oldPosition.x + updatedMove.x;
    //    Bob->Position.y = oldPosition.y + updatedMove.y;

    //    //Bob->Position.x = clamp(Bob->Position.x, 0.f, (f32)TILE_SIZE.x * GameState->Map.Width - TILE_SIZE.x);
    //    //Bob->Position.y = clamp(Bob->Position.y, 0.f, (f32)TILE_SIZE.y * GameState->Map.Height - TILE_SIZE.y);

    //    Bob->Box.Position = Bob->Position;

    //    Bob->Acceleration.y = 10.f;

    //    vec2 idleArea = { 2 * TILE_SIZE.x, 1 * TILE_SIZE.y };

    //    if (updatedMove.x > 0.f) {
    //        if (Bob->Position.x + TILE_SIZE.x > GameState->Camera.x + ScreenWidth / 2 + idleArea.x) {
    //            GameState->Camera.x += updatedMove.x;
    //        }
    //    }
    //    else if (updatedMove.x < 0.f) {
    //        if (Bob->Position.x < GameState->Camera.x + ScreenWidth / 2 - idleArea.x) {
    //            GameState->Camera.x += updatedMove.x;
    //        }
    //    }

    //    if (updatedMove.y > 0.f) {
    //        if (Bob->Position.y + TILE_SIZE.y > GameState->Camera.y + ScreenHeight / 2 + idleArea.y) {
    //            GameState->Camera.y += updatedMove.y;
    //        }
    //    }
    //    else if (updatedMove.y < 0.f) {
    //        if (Bob->Position.y < GameState->Camera.y + ScreenHeight / 2 - idleArea.y) {
    //            GameState->Camera.y += updatedMove.y;
    //        }
    //    }

    //    GameState->Camera.x = GameState->Camera.x > 0.f ? GameState->Camera.x : 0.f;
    //    GameState->Camera.y = GameState->Camera.y > 0.f ? GameState->Camera.y : 0.f;

    //    //if (TILE_SIZE.x * GameState->Map.Width - ScreenWidth >= 0) {
    //    //    GameState->Camera.x = clamp(GameState->Camera.x, 0.f, (f32)TILE_SIZE.x * GameState->Map.Width - ScreenWidth);
    //    //}
    //    //if (TILE_SIZE.y * GameState->Map.Height - ScreenHeight >= 0) {
    //    //    GameState->Camera.y = clamp(GameState->Camera.y, 0.f, (f32)TILE_SIZE.y * GameState->Map.Height - ScreenHeight);
    //    //}

    //    Renderer->glBindBuffer(GL_ARRAY_BUFFER, GameState->VBOParticles);

    //    for (u32 i = 0; i < GameState->ParticleEmittersIndex; ++i) {
    //        particle_emitter *Charge = &GameState->ParticleEmitters[i];

    //        vec2 oldChargePosition = Charge->Box.Position;
    //        vec2 chargeMove = Charge->Velocity * dt;
    //        vec2 chargeTime = vec2(1.f);

    //        for (u32 j = 0; j < GameState->DrawableEntitiesCount; ++j) {
    //            drawable_entity *Entity = &GameState->DrawableEntities[j];

    //            if (Entity->Type == tile_type::REFLECTOR) {
    //                aabb reflectorBox = Entity->Box;

    //                vec2 t = sweptAABB(oldChargePosition, chargeMove, reflectorBox, Charge->Box.Size);

    //                // if not colliding
    //                if (!((0.f <= t.x && t.x < 1.f) || (0.f <= t.y && t.y < 1.f))) {
    //                    if (!intersectAABB(Charge->Box, reflectorBox)) {
    //                        if (Charge->ReflectorIndex == (s32)j) {
    //                            Charge->StopProcessingCollision = false;
    //                            Charge->ReflectorIndex = -1;
    //                        }
    //                        continue;
    //                    }
    //                }

    //                // if collides check direction of the charge
    //                // check reflector's angle
    //                // dimiss charge if it's coming from the wrong side
    //                // proceed with new collision rule otherwise.

    //                if (Charge->StopProcessingCollision && Charge->ReflectorIndex == (s32)j) continue;

    //                aabb testBox = {};

    //                if (chargeMove.x > 0.f) {
    //                    if (Entity->Rotation == 180.f || Entity->Rotation == 270.f) {
    //                        testBox.Position.x = reflectorBox.Position.x + reflectorBox.Size.x / 2.f + Charge->Box.Size.x / 2.f;
    //                        testBox.Position.y = reflectorBox.Position.y;
    //                        testBox.Size.x = reflectorBox.Size.x / 2.f - Charge->Box.Size.x / 2.f;
    //                        testBox.Size.y = reflectorBox.Size.y;

    //                        vec2 t = sweptAABB(oldChargePosition, chargeMove, testBox, Charge->Box.Size);

    //                        if (0.f <= t.x && t.x < 1.f) {
    //                            chargeTime.x = t.x;

    //                            Charge->Velocity.x = 0.f;
    //                            Charge->Velocity.y = Entity->Rotation == 180.f ? chargeVelocity : -chargeVelocity;
    //                            Charge->StopProcessingCollision = true;
    //                            Charge->ReflectorIndex = (s32)j;
    //                        }
    //                    }
    //                    else {
    //                        // collided with outer border: stop processing
    //                        chargeTime = t;
    //                        Charge->IsFading = true;
    //                    }
    //                }
    //                else if (chargeMove.x < 0.f) {
    //                    if (Entity->Rotation == 0.f || Entity->Rotation == 90.f) {
    //                        testBox.Position.x = reflectorBox.Position.x;
    //                        testBox.Position.y = reflectorBox.Position.y;
    //                        testBox.Size.x = reflectorBox.Size.x / 2.f - Charge->Box.Size.x / 2.f;
    //                        testBox.Size.y = reflectorBox.Size.y;

    //                        vec2 t = sweptAABB(oldChargePosition, chargeMove, testBox, Charge->Box.Size);

    //                        if (0.f <= t.x && t.x < 1.f) {
    //                            chargeTime.x = t.x;

    //                            Charge->Velocity.x = 0.f;
    //                            Charge->Velocity.y = Entity->Rotation == 0.f ? -chargeVelocity : chargeVelocity;
    //                            Charge->StopProcessingCollision = true;
    //                            Charge->ReflectorIndex = (s32)j;
    //                        }
    //                    }
    //                    else {
    //                        chargeTime = t;
    //                        Charge->IsFading = true;
    //                    }
    //                }
    //                else if (chargeMove.y > 0.f) {
    //                    if (Entity->Rotation == 0.f || Entity->Rotation == 270.f) {
    //                        testBox.Position.x = reflectorBox.Position.x;
    //                        testBox.Position.y = reflectorBox.Position.y + reflectorBox.Size.y / 2.f + Charge->Box.Size.y / 2.f;
    //                        testBox.Size.x = reflectorBox.Size.x;
    //                        testBox.Size.y = reflectorBox.Size.y / 2.f - Charge->Box.Size.y / 2.f;

    //                        vec2 t = sweptAABB(oldChargePosition, chargeMove, testBox, Charge->Box.Size);

    //                        if (0.f <= t.y && t.y < 1.f) {
    //                            chargeTime.y = t.y;

    //                            Charge->Velocity.x = Entity->Rotation == 0.f ? chargeVelocity : -chargeVelocity;
    //                            Charge->Velocity.y = 0.f;
    //                            Charge->StopProcessingCollision = true;
    //                            Charge->ReflectorIndex = (s32)j;
    //                        }
    //                    }
    //                    else {
    //                        chargeTime = t;
    //                        Charge->IsFading = true;
    //                    }
    //                }
    //                else if (chargeMove.y < 0.f) {
    //                    if (Entity->Rotation == 90.f || Entity->Rotation == 180.f) {
    //                        testBox.Position.x = reflectorBox.Position.x;
    //                        testBox.Position.y = reflectorBox.Position.y;
    //                        testBox.Size.x = reflectorBox.Size.x;
    //                        testBox.Size.y = reflectorBox.Size.y / 2.f - Charge->Box.Size.y / 2.f;

    //                        vec2 t = sweptAABB(oldChargePosition, chargeMove, testBox, Charge->Box.Size);

    //                        if (0.f <= t.y && t.y < 1.f) {
    //                            chargeTime.y = t.y;

    //                            Charge->Velocity.x = Entity->Rotation == 90.f ? chargeVelocity : -chargeVelocity;
    //                            Charge->Velocity.y = 0.f;
    //                            Charge->StopProcessingCollision = true;
    //                            Charge->ReflectorIndex = (s32)j;
    //                        }
    //                    }
    //                    else {
    //                        chargeTime = t;
    //                        Charge->IsFading = true;
    //                    }
    //                }
    //            }

    //            if (Entity->Id == 52) {
    //                vec2 t = sweptAABB(oldChargePosition, chargeMove, Entity->Box, Charge->Box.Size);

    //                if ((0.f <= t.x && t.x < 1.f) || (0.f <= t.y && t.y < 1.f)) {
    //                    Entity->CurrentAnimation = &GameState->Lamp.Animations[0];
    //                    chargeTime = t;
    //                    Charge->IsFading = true;
    //                    Charge->TimeLeft = 0.f;

    //                    drawable_entity *platform1 = GetEntityById(GameState, 57);
    //                    drawable_entity *platform2 = GetEntityById(GameState, 60);
    //                    drawable_entity *platform3 = GetEntityById(GameState, 61);

    //                    // todo: i need smth like setTimeout here
    //                    platform1->CurrentAnimation = &GameState->Platform.Animations[0];
    //                    platform1->StartAnimationDelay = 0.1f;
    //                    platform2->CurrentAnimation = &GameState->Platform.Animations[0];
    //                    platform2->StartAnimationDelay = 0.2f;
    //                    platform3->CurrentAnimation = &GameState->Platform.Animations[0];
    //                    platform3->StartAnimationDelay = 0.3f;
    //                }
    //            }
    //        }

    //        chargeMove *= chargeTime;
    //        Charge->Box.Position += chargeMove;
    //        Charge->Position += chargeMove;

    //        //if (Charge->Box.Position.x <= 0.f || Charge->Box.Position.x >= (f32)TILE_SIZE.x * GameState->Map.Width) {
    //        //    Charge->Velocity.x = -Charge->Velocity.x;
    //        //}
    //        //if (Charge->Box.Position.y <= 0.f || Charge->Box.Position.y >= (f32)TILE_SIZE.y * GameState->Map.Height) {
    //        //    Charge->Velocity.y = -Charge->Velocity.y;
    //        //}

    //        b32 chargeCollide = intersectAABB(Swoosh->Box, GameState->ParticleEmitters[0].Box);
    //        if (chargeCollide && Swoosh->ShouldRender && GameState->ChargeSpawnCooldown > 1.f) {
    //            GameState->ChargeSpawnCooldown = 0.f;

    //            particle_emitter NewCharge = GameState->ParticleEmitters[0];        // copy
    //            NewCharge.Particles = PushArray<particle>(&GameState->WorldArena, NewCharge.ParticlesCount);
    //            NewCharge.Velocity.x = Bob->Flipped ? -chargeVelocity : chargeVelocity;

    //            GameState->ParticleEmitters[GameState->ParticleEmittersIndex++] = NewCharge;
    //        }
    //    }

    //    for (u32 ParticleEmitterIndex = 0; ParticleEmitterIndex < GameState->ParticleEmittersIndex; ++ParticleEmitterIndex) {
    //        particle_emitter *ParticleEmitter = &GameState->ParticleEmitters[ParticleEmitterIndex];

    //        if (ParticleEmitter->IsFading) {
    //            ParticleEmitter->TimeLeft -= Params->Delta;
    //        }

    //        if (ParticleEmitter->TimeLeft <= 0.f) {
    //            // todo: read access violation
    //            //particle_emitters->erase(particle_emitters->begin() + i);
    //            --GameState->ParticleEmittersIndex;
    //        }
    //    }

    //    u64 particlesSize = 0;
    //    // todo: use transform feedback instead?
    //    for (u32 ParticleEmitterIndex = 0; ParticleEmitterIndex < GameState->ParticleEmittersIndex; ++ParticleEmitterIndex) {
    //        particle_emitter *ParticleEmitter = &GameState->ParticleEmitters[ParticleEmitterIndex];

    //        if (ParticleEmitter->TimeLeft <= 0.f) {
    //            particlesSize += ParticleEmitter->ParticlesCount * sizeof(particle);
    //            continue;
    //        };

    //        for (u32 j = 0; j < ParticleEmitter->NewParticlesCount; ++j) {
    //            u32 unusedParticleIndex = FindFirstUnusedParticle(ParticleEmitter);
    //            ParticleEmitter->LastUsedParticle = unusedParticleIndex;

    //            particle *Particle = &ParticleEmitter->Particles[unusedParticleIndex];

    //            // respawing particle
    //            f32 randomX = randomInRange(-1.f * Scale.x, 1.f * Scale.x);
    //            f32 randomY = randomInRange(-1.f * Scale.y, 1.f * Scale.y);

    //            Particle->Lifespan = 1.f;
    //            Particle->Position.x = ParticleEmitter->Position.x + randomX;
    //            Particle->Position.y = ParticleEmitter->Position.y + randomY;
    //            Particle->Size = { 0.2f * TILE_SIZE.x, 0.2f * TILE_SIZE.y };
    //            Particle->Velocity = { 0.f, 0.f };
    //            Particle->Acceleration = { randomX * 10.f, 10.f };
    //            Particle->UV = vec2((13 * (Tileset->TileSize.y + Tileset->Spacing) + Tileset->Margin) / (f32)Tileset->Image.Height,
    //                (16 * (Tileset->TileSize.y + Tileset->Spacing) + Tileset->Margin) / (f32)Tileset->Image.Height);
    //            Particle->Alpha = 1.f;
    //        }

    //        for (u32 j = 0; j < ParticleEmitter->ParticlesCount; ++j) {
    //            particle *P = &ParticleEmitter->Particles[j];
    //            f32 dt = ParticleEmitter->Dt;

    //            if (P->Lifespan > 0.f) {
    //                P->Lifespan -= (f32)dt;
    //                P->Velocity = P->Acceleration * dt;
    //                P->Position.x += randomInRange(-1.f, 1.f);
    //                P->Position.y += randomInRange(-1.f, 1.f);
    //                P->Alpha -= (f32)dt * 1.f;
    //                P->Size -= (f32)dt * 1.f;
    //            }
    //        }

    //        Renderer->glBufferSubData(GL_ARRAY_BUFFER, particlesSize, ParticleEmitter->ParticlesCount * sizeof(particle),
    //            ParticleEmitter->Particles);

    //        particlesSize += ParticleEmitter->ParticlesCount * sizeof(particle);
    //    }

    //    GameState->Lag -= GameState->UpdateRate;
    //}

    //GameState->ChargeSpawnCooldown += Params->Delta;

    //mat4 view = mat4(1.0f);
    //view = glm::translate(view, vec3(-GameState->Camera, 0.f));
    //setShaderUniform(Memory, GameState->ViewUniformLocation, view);

    //--- drawing tilemap ---
    //Renderer->glBindBuffer(GL_ARRAY_BUFFER, GameState->VBOTiles);
    //setShaderUniform(Memory, GameState->TypeUniformLocation, 1);

    //mat4 model = mat4(1.0f);
    //model = glm::scale(model, vec3(TILE_SIZE, 1.f));
    //setShaderUniform(Memory, GameState->ModelUniformLocation, model);

    //Renderer->glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, GameState->TilesCount);

    //--- bob ---
    //f32 bobXOffset = (f32)(Bob->CurrentAnimation.X * (Tileset->TileSize.x + Tileset->Spacing) + Tileset->Margin) / Tileset->Image.Width;
    //f32 bobYOffset = (f32)(Bob->CurrentAnimation.Y * (Tileset->TileSize.y + Tileset->Spacing) + Tileset->Margin) / Tileset->Image.Height;

    //if (Bob->FrameTime >= Bob->CurrentAnimation.Delay) {
    //    Bob->XAnimationOffset += (GameState->SpriteWidth + (f32)Tileset->Spacing / Tileset->Image.Width) * Bob->CurrentAnimation.Size;
    //    if (Bob->XAnimationOffset >= ((Bob->CurrentAnimation.Frames * Tileset->TileSize.x * Bob->CurrentAnimation.Size) / (f32)Tileset->Image.Width)) {
    //        Bob->XAnimationOffset = 0.f;
    //        if (Bob->CurrentAnimation == Bob->Animations[1] || Bob->CurrentAnimation == Bob->Animations[3]) {
    //            Bob->CurrentAnimation = Bob->Animations[0];
    //        }
    //    }

    //    Bob->FrameTime = 0.0f;
    //}
    //Bob->FrameTime += Params->Delta;

    //model = mat4(1.0f);
    //model = glm::scale(model, vec3(TILE_SIZE, 1.f));
    //setShaderUniform(Memory, GameState->ModelUniformLocation, model);

    //f32 effectXOffset = (f32)(Swoosh->CurrentAnimation.X * (Tileset->TileSize.x + Tileset->Spacing) + Tileset->Margin) / Tileset->Image.Width;
    //f32 effectYOffset = (f32)(Swoosh->CurrentAnimation.Y * (Tileset->TileSize.y + Tileset->Spacing) + Tileset->Margin) / Tileset->Image.Height;

    //if (Swoosh->FrameTime >= Swoosh->CurrentAnimation.Delay) {
    //    Swoosh->XAnimationOffset += (GameState->SpriteWidth + (f32)Tileset->Spacing / Tileset->Image.Width) * Swoosh->CurrentAnimation.Size;
    //    if (Swoosh->XAnimationOffset >= ((Swoosh->CurrentAnimation.Frames * Tileset->TileSize.x * Swoosh->CurrentAnimation.Size) / (f32)Tileset->Image.Width)) {
    //        Swoosh->XAnimationOffset = 0.f;
    //        Swoosh->ShouldRender = false;
    //    }

    //    Swoosh->FrameTime = 0.0f;
    //}

    //if (Swoosh->ShouldRender) {
    //    Swoosh->FrameTime += Params->Delta;
    //}

    //Renderer->glBindBuffer(GL_ARRAY_BUFFER, GameState->VBOEntities);
    //// handling animations on all entities
    //for (u32 DrawableEntityIndex = 0; DrawableEntityIndex < GameState->DrawableEntitiesCount; ++DrawableEntityIndex) {
    //    drawable_entity *Entity = &GameState->DrawableEntities[DrawableEntityIndex];

    //    if (Entity->CurrentAnimation) {
    //        if (Entity->StartAnimationDelayTimer < Entity->StartAnimationDelay) {
    //            Entity->StartAnimationDelayTimer += Params->Delta;
    //            break;
    //        }

    //        f32 entityXOffset = (f32)(Entity->CurrentAnimation->X * (Tileset->TileSize.x + Tileset->Spacing) + Tileset->Margin) / Tileset->Image.Width;
    //        f32 entityYOffset = (f32)(Entity->CurrentAnimation->Y * (Tileset->TileSize.y + Tileset->Spacing) + Tileset->Margin) / Tileset->Image.Height;

    //        if (Entity->FrameTime >= Entity->CurrentAnimation->Delay) {
    //            if (Entity->CurrentAnimation->Direction == direction::RIGHT) {
    //                Entity->XAnimationOffset += (GameState->SpriteWidth + (f32)Tileset->Spacing / Tileset->Image.Width) * Entity->CurrentAnimation->Size;
    //            }
    //            else if (Entity->CurrentAnimation->Direction == direction::LEFT) {
    //                Entity->XAnimationOffset -= (GameState->SpriteWidth + (f32)Tileset->Spacing / Tileset->Image.Width) * Entity->CurrentAnimation->Size;
    //            }

    //            Entity->UV = vec2(entityXOffset + Entity->XAnimationOffset, entityYOffset);
    //            Renderer->glBufferSubData(GL_ARRAY_BUFFER, Entity->Offset + Offset(drawable_entity, UV), sizeof(vec2), &Entity->UV);

    //            if (abs(Entity->XAnimationOffset) >= ((Entity->CurrentAnimation->Frames * Tileset->TileSize.x * Entity->CurrentAnimation->Size) / (f32)Tileset->Image.Width)) {
    //                Entity->XAnimationOffset = 0.f;

    //                Entity->StartAnimationDelayTimer = 0.f;
    //                Entity->StartAnimationDelay = 0.f;
    //                Entity->CurrentAnimation = nullptr;
    //            }

    //            Entity->FrameTime = 0.0f;
    //        }
    //        Entity->FrameTime += Params->Delta;
    //    }
    //}

    ////--- drawing entities ---
    //setShaderUniform(Memory, GameState->TypeUniformLocation, 3);

    //GameState->Player.UV = vec2(bobXOffset + Bob->XAnimationOffset, bobYOffset);
    //GameState->Player.Position = Bob->Position;
    //GameState->Player.Box.Position = Bob->Box.Position;
    //GameState->Player.Flipped = Bob->Flipped;

    //GameState->SwooshEffect.UV = vec2(effectXOffset + Swoosh->XAnimationOffset, effectYOffset);
    //GameState->SwooshEffect.Position = Swoosh->Position;
    //GameState->SwooshEffect.Box.Position = Swoosh->Box.Position;
    //GameState->SwooshEffect.Flipped = Bob->Flipped;
    //GameState->SwooshEffect.ShouldRender = Swoosh->ShouldRender ? 1 : 0;

    //Renderer->glBufferSubData(GL_ARRAY_BUFFER, GameState->Player.Offset + Offset(drawable_entity, Position), 2 * sizeof(f32), &GameState->Player.Position);
    //Renderer->glBufferSubData(GL_ARRAY_BUFFER, GameState->SwooshEffect.Offset + Offset(drawable_entity, Position), 2 * sizeof(f32), &GameState->SwooshEffect.Position);
    //Renderer->glBufferSubData(GL_ARRAY_BUFFER, GameState->Player.Offset + Offset(drawable_entity, UV), 2 * sizeof(f32), &GameState->Player.UV);
    //Renderer->glBufferSubData(GL_ARRAY_BUFFER, GameState->SwooshEffect.Offset + Offset(drawable_entity, UV), 2 * sizeof(f32), &GameState->SwooshEffect.UV);
    //Renderer->glBufferSubData(GL_ARRAY_BUFFER, GameState->Player.Offset + Offset(drawable_entity, Flipped), sizeof(u32), &GameState->Player.Flipped);
    //Renderer->glBufferSubData(GL_ARRAY_BUFFER, GameState->SwooshEffect.Offset + Offset(drawable_entity, Flipped), sizeof(u32), &GameState->SwooshEffect.Flipped);
    //Renderer->glBufferSubData(GL_ARRAY_BUFFER, GameState->SwooshEffect.Offset + Offset(drawable_entity, ShouldRender), sizeof(u32), &GameState->SwooshEffect.ShouldRender);

    //Renderer->glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (s32)GameState->DrawableEntitiesCount);

    ////--- drawing particles ---
    //Renderer->glBindBuffer(GL_ARRAY_BUFFER, GameState->VBOParticles);
    //setShaderUniform(Memory, GameState->TypeUniformLocation, 4);

    //Renderer->glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    //// todo: offsets when delete in the middle?
    //s32 totalParticlesCount = 0;
    //for (u32 ParticleEmitterIndex = 0; ParticleEmitterIndex < GameState->ParticleEmittersIndex; ++ParticleEmitterIndex) {
    //    totalParticlesCount += GameState->ParticleEmitters[ParticleEmitterIndex].ParticlesCount;
    //}

    //// todo: draw only the ones which lifespan is greater than zero
    //Renderer->glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, totalParticlesCount);

    //std::cout << delta * 1000.f << " ms" << std::endl;
}