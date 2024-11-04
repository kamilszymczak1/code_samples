package lab04.assignments;

import java.util.ArrayList;
import java.util.List;
import java.util.function.IntBinaryOperator;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.Map;

public class MatrixRowSums {
    private static final int N_ROWS = 100000;
    private static final int N_COLUMNS = 4;

    private static IntBinaryOperator matrixDefinition = (row, col) -> {
        int a = 2 * col + 1;
        for (int i = 0; i < 2000; i++) // Simulate resource-expensive computation.
            a = (a * a) % 1000;
        return (row + 1) * (a % 4 - 2) * a;
    };

    private static void printRowSumsSequentially() {
        long start = System.currentTimeMillis();
        for (int r = 0; r < N_ROWS; ++r) {
            int sum = 0;
            for (int c = 0; c < N_COLUMNS; ++c) {
                sum += matrixDefinition.applyAsInt(r, c);
            }
            System.out.println(r + " -> " + sum);
        }
        long end = System.currentTimeMillis();
        System.out.println("Sequential Time: " + (end - start) + " ms");
    }

    private static class RowInfo {
        private final AtomicInteger sum;
        private final CountDownLatch latch;
        public RowInfo(AtomicInteger sum, CountDownLatch latch) {
            this.sum = sum;
            this.latch = latch;
        }

        public AtomicInteger getSum() {
            return sum;
        }

        public CountDownLatch getLatch() {
            return latch;
        }
    }

    private static class Worker implements Runnable {
        private final int column;
        Map<Integer, RowInfo> rowInfos;

        public Worker(int column,
                      Map<Integer, RowInfo> rowInfos) {
            this.column = column;
            this.rowInfos = rowInfos;
        }

        @Override
        public void run() {
            for (int i = 0; i < N_ROWS; i++) {

                int entry = matrixDefinition.applyAsInt(i, column);

                rowInfos.putIfAbsent(i, new RowInfo(new AtomicInteger(0), new CountDownLatch(N_COLUMNS)));
                RowInfo info = rowInfos.get(i);
                info.getSum().addAndGet(entry);
                info.getLatch().countDown();
            }
        }
    }

    private static void printRowSumsInParallel() throws InterruptedException {
        List<Thread> threads = new ArrayList<>();

        Map<Integer, RowInfo> rowInfos = new ConcurrentHashMap<>();

        for (int c = 0; c < N_COLUMNS; ++c) {
            final int myColumn = c;
            threads.add(new Thread(new Worker(myColumn, rowInfos)));
        }
        for (Thread t : threads) {
            t.start();
        }

        long start = System.currentTimeMillis();

        for (int i = 0; i < N_ROWS; i++) {
            rowInfos.putIfAbsent(i, new RowInfo(new AtomicInteger(0), new CountDownLatch(N_COLUMNS)));
            RowInfo rowInfo = rowInfos.get(i);
            rowInfo.getLatch().await();
            System.out.println(i + " -> " + sums.get(i));
            rowInfos.remove(i);
        }

        long end = System.currentTimeMillis();
        System.out.println("Concurrent Time: " + (end - start) + " ms");

        try {
            for (Thread t : threads) {
                t.join();
            }
        } catch (InterruptedException e) {
            for (Thread t : threads) {
                t.interrupt();
            }
            throw e;
        }
    }

    public static void main(String[] args) {
        try {
            System.out.println("-- Sequentially --");
            printRowSumsSequentially();
            System.out.println("-- In parallel --");
            printRowSumsInParallel();
            System.out.println("-- End --");
        } catch (InterruptedException e) {
            System.err.println("Main interrupted.");
        }
    }
}
