package net.minecraft.client.gui.components.debug;

import java.util.List;
import java.util.Locale;
import net.minecraft.resources.Identifier;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.chunk.LevelChunk;
import org.jspecify.annotations.Nullable;

public class DebugEntryDetailedMemory implements DebugScreenEntry {
   private static final Identifier GROUP = Identifier.withDefaultNamespace("memory");

   @Override
   public void display(
      final DebugScreenDisplayer displayer,
      final @Nullable Level serverOrClientLevel,
      final @Nullable LevelChunk clientChunk,
      final @Nullable LevelChunk serverChunk
   ) {
      // ART 补丁：用 Runtime 代替 MemoryMXBean
      Runtime rt = Runtime.getRuntime();
      long max = rt.maxMemory(), total = rt.totalMemory(), free = rt.freeMemory();
      displayer.addToGroup(
         GROUP,
         List.of(String.format(Locale.ROOT, "Memory (heap): i=000MiB u=%03dMiB c=%03dMiB m=%03dMiB",
            bytesToMebibytes(total - free), bytesToMebibytes(total), bytesToMebibytes(max)))
      );
   }

   private static long bytesToMebibytes(final long used) {
      return used / 1024L / 1024L;
   }

   @Override
   public boolean isAllowed(final boolean reducedDebugInfo) {
      return true;
   }
}
