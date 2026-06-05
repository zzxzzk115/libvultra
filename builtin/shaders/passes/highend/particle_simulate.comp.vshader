[vshader]
language = glsl
version = 460

[comp]
// GPU particle simulation (fixed-pool, round-robin respawn).
//
// Each invocation owns one pool slot. A round-robin window [cursor, cursor + emitCount) is respawned
// from the emitter parameters; every other living slot is integrated under gravity and aged out; dead
// slots (lifetime <= 0) stay dead. A freshly created/resized pool sets the reset bit so all
// non-spawned slots are seeded as dead without a host-side clear.
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

struct GpuParticle
{
    vec4 positionAge;  // xyz position, w age
    vec4 velocityLife; // xyz velocity, w lifetime (<= 0 => dead)
};

layout(set = 0, binding = 0, std430) buffer ParticleBuffer
{
    GpuParticle particles[];
} s_Particles;

layout(push_constant) uniform ParticlePushConstants
{
    vec4  originAndDt;       // xyz origin, w dt
    vec4  startVelAndRadius; // xyz start velocity, w spawn radius
    vec4  gravityAndVelVar;  // xyz gravity, w velocity variance
    vec4  lifeAndSizes;      // x lifetime, y lifetimeVariance, z startSize, w endSize
    vec4  startColor;
    vec4  endColor;
    uvec4 counts;            // x maxParticles, y emitCount, z spawnCursor|resetBit, w frameSeed
} u_PC;

const uint kResetBit   = 0x80000000u;
const uint kCursorMask = 0x7FFFFFFFu;

uint pcgHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rndNext(inout uint state)
{
    state = pcgHash(state);
    return float(state) * (1.0 / 4294967296.0);
}

vec3 randUnitVec(inout uint state)
{
    float z = rndNext(state) * 2.0 - 1.0;
    float a = rndNext(state) * 6.28318530718;
    float r = sqrt(max(0.0, 1.0 - z * z));
    return vec3(r * cos(a), r * sin(a), z);
}

void main()
{
    uint i            = gl_GlobalInvocationID.x;
    uint maxParticles = u_PC.counts.x;
    if (i >= maxParticles)
        return;

    uint  emitCount = u_PC.counts.y;
    uint  cursor    = u_PC.counts.z & kCursorMask;
    bool  resetAll  = (u_PC.counts.z & kResetBit) != 0u;
    uint  frameSeed = u_PC.counts.w;
    float dt        = u_PC.originAndDt.w;

    // Round-robin spawn window, wrapped over the pool.
    uint rel     = (i + maxParticles - cursor) % maxParticles;
    bool respawn = emitCount > 0u && rel < emitCount;

    GpuParticle p = s_Particles.particles[i];

    if (respawn)
    {
        uint  seed     = pcgHash(frameSeed ^ (i * 2654435761u));
        float lifetime = max(0.05, u_PC.lifeAndSizes.x * (1.0 + (rndNext(seed) * 2.0 - 1.0) * u_PC.lifeAndSizes.y));
        vec3  pos      = u_PC.originAndDt.xyz + randUnitVec(seed) * (rndNext(seed) * u_PC.startVelAndRadius.w);
        vec3  vel      = u_PC.startVelAndRadius.xyz + randUnitVec(seed) * (rndNext(seed) * u_PC.gravityAndVelVar.w);
        p.positionAge  = vec4(pos, 0.0);
        p.velocityLife = vec4(vel, lifetime);
    }
    else if (resetAll)
    {
        // Uninitialised pool memory: seed every non-spawned slot as dead.
        p.positionAge  = vec4(0.0);
        p.velocityLife = vec4(0.0);
    }
    else
    {
        float lifetime = p.velocityLife.w;
        if (lifetime > 0.0)
        {
            float age = p.positionAge.w + dt;
            if (age < lifetime)
            {
                vec3 vel           = p.velocityLife.xyz + u_PC.gravityAndVelVar.xyz * dt;
                p.velocityLife.xyz = vel;
                p.positionAge.xyz += vel * dt;
                p.positionAge.w    = age;
            }
            else
            {
                p.velocityLife.w = 0.0; // died this frame
            }
        }
    }

    s_Particles.particles[i] = p;
}
