package irvine.math.lattice;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/** Single-index verifier for OEIS A049230 using the pinned jOEIS walker. */
public final class A049230Verifier {

  private A049230Verifier() {
  }

  private record Seed(long[] points, int weight, int axesMask) {
  }

  private static long serialCount(final int n, final Lattice lattice) {
    final ExactContactsWalker walker = new ExactContactsWalker(lattice, 2);
    final long firstStep = lattice.toPoint(1, 0, 0);
    return walker.count(n, 6, 1, lattice.origin(), firstStep);
  }

  private static long split3Count(final int n, final Lattice lattice)
      throws InterruptedException, ExecutionException {
    final long origin = lattice.origin();
    final long firstStep = lattice.toPoint(1, 0, 0);
    final List<Seed> seeds = List.of(
        new Seed(new long[] {origin, firstStep, lattice.toPoint(2, 0, 0)}, 6, 1),
        new Seed(new long[] {origin, firstStep, lattice.toPoint(1, 1, 0)}, 12, 3),
        new Seed(new long[] {origin, firstStep, lattice.toPoint(1, 0, 1)}, 12, 5));
    final ExecutorService executor = Executors.newFixedThreadPool(seeds.size());
    try {
      final List<Callable<Long>> calls = new ArrayList<>();
      for (final Seed seed : seeds) {
        calls.add(() -> {
          final ExactContactsWalker walker = new ExactContactsWalker(lattice, 2);
          return walker.count(n, seed.weight(), seed.axesMask(), seed.points());
        });
      }
      long total = 0;
      for (final Future<Long> future : executor.invokeAll(calls)) {
        total = Math.addExact(total, future.get());
      }
      return total;
    } finally {
      executor.shutdownNow();
    }
  }

  public static void main(final String[] args)
      throws InterruptedException, ExecutionException {
    if (args.length != 2 || !args[0].matches("[1-9][0-9]*")
        || !(args[1].equals("serial") || args[1].equals("split3"))) {
      System.err.println("usage: A049230Verifier N serial|split3");
      System.exit(2);
    }
    final int n;
    try {
      n = Integer.parseInt(args[0]);
    } catch (final NumberFormatException error) {
      System.err.println("N is outside the supported integer grammar");
      System.exit(2);
      return;
    }
    if (n < 1 || n > 19) {
      System.err.println("N must be in 1..19");
      System.exit(2);
    }
    if (n == 1) {
      System.out.println("1 0");
      return;
    }

    final Lattice lattice = new CubicLattice();
    final long start = System.nanoTime();
    System.err.println("START verifier=joeis n=" + n + " mode=" + args[1]);
    final ScheduledExecutorService heartbeat = Executors.newSingleThreadScheduledExecutor();
    heartbeat.scheduleAtFixedRate(
        () -> System.err.println("HEARTBEAT verifier=joeis n=" + n
            + " elapsed_s=" + TimeUnit.NANOSECONDS.toSeconds(System.nanoTime() - start)),
        300, 300, TimeUnit.SECONDS);
    final long count;
    try {
      count = args[1].equals("serial") || n == 2
          ? serialCount(n, lattice)
          : split3Count(n, lattice);
    } finally {
      heartbeat.shutdownNow();
    }
    System.err.println("COMPLETE verifier=joeis n=" + n
        + " elapsed_s=" + TimeUnit.NANOSECONDS.toSeconds(System.nanoTime() - start));
    System.out.println(n + " " + count);
  }
}
