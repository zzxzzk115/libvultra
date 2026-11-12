# OpenXR Vulkan Example Models

Used by `example-xr`. `Hand.obj` and `Car.obj` are unmodified copies from
[janhsimon/openxr-vulkan-example](https://github.com/janhsimon/openxr-vulkan-example) at revision
`4ff50bb9019c843cab8251a492fc5513ac35188d`, which lists these sources:

| File | Title | Author | License |
| --- | --- | --- | --- |
| `Hand.obj` | [Hand Anatomy Reference](https://sketchfab.com/3d-models/hand-anatomy-reference-2024d77a872a45f1baf6d81b51fe18a6) | [Ant B-D](https://sketchfab.com/ant_bd) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| `Car.obj` | [GAZ-21](https://sketchfab.com/3d-models/gaz-21-fdf5cbff00b04ab99a6ea3ab6b46533e) | [Ashkelon](https://sketchfab.com/Ashkelon) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |

The upstream repository carries the OBJ files without the material files they name. `Hand.mtl` and `Car.mtl`
were written for Vultra and hold one untextured diffuse colour each; they are not part of the original models.

The hand is authored as a left hand in the controller's aim pose; the example mirrors it for the right hand.
These models keep their own licenses and are not covered by this repository's MIT license.
