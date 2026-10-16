#ifndef GARRYSMOD_LUA_AUTHORITATIVE_TYPES_H
#define GARRYSMOD_LUA_AUTHORITATIVE_TYPES_H
// GarrysMod::Lua::Type enum - the values the reference win64 build uses.
// GetType returns these values and GetTypeName indexes a 44-entry name table.
namespace GarrysMod
{
    namespace Lua
    {
        namespace Type
        {
            enum
            {
                None = -1,
                Nil,
                Bool,
                LightUserData,
                Number,
                String,
                Table,
                Function,
                UserData,
                Thread,

                Entity,
                Vector,
                Angle,
                PhysObj,
                Save,
                Restore,
                DamageInfo,
                EffectData,
                MoveData,
                RecipientFilter,
                UserCmd,
                ScriptedVehicle,
                Material,
                Panel,
                Particle,
                ParticleEmitter,
                Texture,
                UserMsg,
                ConVar,
                IMesh,
                Matrix,
                Sound,
                PixelVisHandle,
                DLight,
                Video,
                File,
                Locomotion,
                Path,
                NavArea,
                SoundHandle,
                NavLadder,
                ParticleSystem,
                ProjectedTexture,
                PhysCollide,
                SurfaceInfo,

                Type_Count
            };
        }
    }
}
#endif
