# Tidy

A gtkmm-4 app that knows how long each file in your home folder should live,
and where it belongs -- and acts on it only with your say-so, safely and
undoably.

Files arrive without intent: a save lands wherever the app pointed. Each file's
life is set by its purpose, not by how often it is read. Tidy gives every file
a **retention level**, mostly decided when it is born; the undecided wait in
**the Closet** until someone decides. Projects (`<name>-project`) move whole
and keep their tree. Nothing is removed until it is archived, cataloged and
on backup media.

Split from Rouster (the space reclaimer) in Nov 2026; started from Cairn; file
moves go through the shared door library Rouster also uses.

Status: design. No code yet.
