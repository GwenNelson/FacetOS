# Initrd source trees and overlays

`make build` packs these source trees with `tools/facet-initrd`. Local files in
`.facet-overlays/dominit0`, `.facet-overlays/system`, and
`.facet-overlays/child` are merged last and are deliberately ignored by Git so
they persist across incremental builds without becoming project content.

The build applies the UID/GID and mode fixtures used by `TestPerms` after
merging overlays. Use `tools/facet-initrd list build/initrd/system.initrd` to
inspect the exact metadata placed in an image.
