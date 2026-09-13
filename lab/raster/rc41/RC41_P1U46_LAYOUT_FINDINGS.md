# RC41 — P1U46 logical/physical geometry findings

Static reverse engineering target: RC40/P1U46 runtime SHA256 `774f88c976ed296d7496d5fce1fad9f8e1056337ee70e0111371866074e75669`.

## Distinct geometry globals

P1U46 already contains distinct storage for several geometry concepts:

- configured render target: RVA `0x0004B000/0x0004B004` (field value 1280x720);
- configured native output: RVA `0x0004B040/0x0004B044` (field value 1920x1080);
- selected low-resolution render request: RVA `0x02C3FB78/0x02C3FB7C` (1280x720);
- P1U46 logical game mode cache: RVA `0x02C7E060/0x02C7E064` (field value 1280x720);
- actual engine-facing swapchain buffer: RVA `0x02C7E1B8/0x02C7E1BC` (1280x720).

At `D3D11CreateDeviceAndSwapChain+0x1e80`, P1U46 queries the real swapchain descriptor and stores width/height in `0x02C7E1B8/1BC`, then copies those values into `0x0004B000/004` and `0x02C7E060/064`. Therefore the current design explicitly collapses logical mode onto physical buffer geometry after creation.

Additional write sites copy physical geometry back into logical cache during initialization/reset paths at RVAs approximately `0xD163/0xD169` and `0xFA1D/0xFA23`. A one-shot post-creation memory write is therefore not an acceptable production solution.

## Existing logical DXGI hooks are physically keyed

The installed containing-output hooks already virtualize DXGI mode APIs, but their target width/height comes from `0x0004B000/004`, i.e. the configured physical render geometry, not `0x0004B040/044` native output geometry.

For example the FindClosestMatchingMode wrapper around RVA `0x2FEB0` builds a temporary mode whose width/height are loaded from `0x0004B000/004` before forwarding the call. The mode-list filtering helper around RVA `0x301A0` also filters against `0x0004B000/004`.

Thus `logical DXGI` in P1U46 currently means “present the low-resolution render mode consistently to the game”, not “preserve native logical composition”.

## Important safety result

Do **not** simply rewrite `0x02C7E060/064` to 1920x1080 in production. Internal P1U46 state machines compare the logical cache against `0x02C7E1B8/1BC`; forcing them apart can trigger mismatch/recovery paths. Likewise do **not** rewrite `0x0004B000/004`: those values are used broadly by physical render/root-authority logic and existing DXGI hooks.

The RC41 production bridge therefore needs query-level/native logical virtualization plus selective physical raster/resource mapping, not global geometry mutation or polling.

## Consequence for RC41

The validated RC41 viewport/scissor layer is necessary but not sufficient. To make the engine compose in native logical coordinates without allocating the heavy render family natively, RC41 must add a selective resource-family bridge (CreateTexture2D/RTV/DSV family classification) and preserve low-resolution swapchain/storage while returning native geometry through the game-facing logical interfaces.
