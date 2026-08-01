# Internal authoring tools built on the Nara engine. Each is its own executable.
add_subdirectory(editor)
add_subdirectory(player)
add_dependencies(NaraGame NaraGamePlayer)
