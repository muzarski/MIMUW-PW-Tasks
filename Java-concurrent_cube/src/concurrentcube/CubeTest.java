package concurrentcube;

import org.junit.jupiter.api.Test;

import java.util.Random;
import java.util.concurrent.BrokenBarrierException;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.jupiter.api.Assertions.*;

class CubeTest {

    public boolean isCubeCorrect(Cube cube) throws InterruptedException {
        String cur = cube.show();
        boolean result = true;
        int[] counters = new int[6];
        for (int k = 0; k < 6; k++) counters[k] = 0;
        for (int k = 0; k < cur.length(); k++) {
            counters[Character.getNumericValue(cur.charAt(k))]++;
        }
        
        int expected = cube.getSize() * cube.getSize();
        for (int c : counters) {
            result = result && (c == expected);
        }
        return result;
    }
    
    // Executing 10^6 rotations on the cube.
    // Then, executing opposite rotations so the cube should be solved at the end.
    @Test
    public void sequentialCorrectness() throws InterruptedException {
        int cubeSize = 10;
        
        Cube cube = new Cube(cubeSize, 
                            (x, y) -> {},
                            (x, y) -> {},
                            () -> {},
                            () -> {});

        String begin = cube.show();

        int n = 1000000;
        int[] sides = new int[n];
        int[] layers = new int[n];
        Random r = new Random();
        for (int i = 0; i < n; i++) {
            int curSide = r.nextInt(6);
            int curLayer = r.nextInt(6);
            sides[i] = curSide;
            layers[i] = curLayer;
            cube.rotate(curSide, curLayer);
        }

        for (int i = n - 1; i >= 0; i--) {
            cube.rotate(Cube.oppSide(sides[i]), cubeSize - 1 - layers[i]);
        }
        
        assertEquals(cube.show(), begin);
    }
    
    // Testing whether multiple threads are recessed to the critical section
    // (since they perform rotations of the layers that do not intersect).
    @Test
    public void smallConcurrencyTest() throws InterruptedException {
        int cubeSize = 10;
        AtomicBoolean result = new AtomicBoolean(false);
        CyclicBarrier barrier = new CyclicBarrier(cubeSize, () -> {result.set(true);});
        Cube cube = new Cube(cubeSize,
                            (x, y) -> {
                                try {
                                    barrier.await();
                                } catch (InterruptedException | BrokenBarrierException e) {
                                    e.printStackTrace();
                                }
                            },
                            (x, y) -> {}, () -> {}, () -> {});
        
        Thread[] threads = new Thread[cubeSize];
        for (int i = 0; i < cubeSize; i++) {
            int j = i;
            threads[i] = new Thread(() -> {
                try {
                    Random r = new Random();
                    int x = r.nextInt(2);
                    if (x == 0) cube.rotate(0, j);
                    else cube.rotate(5, cubeSize - 1 - j);
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            });
        }
        
        for (Thread t : threads) t.start();
        for (Thread t : threads) t.join();
        
        assertTrue(result.get());
    }

    // Stress test - testing security.
    // Executing 10^6 random rotations on a 7-sized cube.
    // After every rotation checks whether the state of the cube is correct.
    @Test
    public void securityTest() throws InterruptedException, ExecutionException {
        AtomicInteger counter = new AtomicInteger(0);
        int cubeSize = 7;
        AtomicInteger maxCounter = new AtomicInteger(0);
        Cube cube = new Cube(cubeSize,
                (x, y) -> { maxCounter.set(Math.max(maxCounter.get(), counter.incrementAndGet())); },
                (x, y) -> {  counter.decrementAndGet(); },
                () -> {},
                () -> {});

        AtomicBoolean cubeCorrectness = new AtomicBoolean(true);
        int threadsCount = 10;
        int rotations = 1000000;
        
        Executors.newFixedThreadPool(threadsCount).submit(() -> {
            Random r = new Random();
            for (int j = 0; j < rotations; j++) {
                int side = r.nextInt(6);
                int layer = r.nextInt(cubeSize);
                try {
                    cube.rotate(side, layer);
                    cubeCorrectness.set(cubeCorrectness.get() && isCubeCorrect(cube));
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }).get();
        
        assertTrue(cubeCorrectness.get());
        assertEquals(0, counter.get());
        assertTrue(maxCounter.get() <= cubeSize);
    }

    // Stress test - testing security.
    // Executing 10^7 random rotations on a 10-sized cube.
    // After every rotation checks whether the state of the cube is correct.
    @Test
    public void bigSecurityTest() throws InterruptedException, ExecutionException {
        AtomicInteger counter = new AtomicInteger(0);
        int cubeSize = 10;
        AtomicInteger maxCounter = new AtomicInteger(0);
        Cube cube = new Cube(cubeSize,
                (x, y) -> { maxCounter.set(Math.max(maxCounter.get(), counter.incrementAndGet())); },
                (x, y) -> {  counter.decrementAndGet(); },
                () -> {},
                () -> {});
        
        int threadsCount = 10;
        int rotations = 10000000;

        Executors.newFixedThreadPool(threadsCount).submit(() -> {
            Random r = new Random();
            for (int j = 0; j < rotations; j++) {
                int side = r.nextInt(6);
                int layer = r.nextInt(cubeSize);
                try {
                    cube.rotate(side, layer);
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }).get();
        
        assertTrue(isCubeCorrect(cube));
        assertEquals(0, counter.get());
        assertTrue(maxCounter.get() <= cubeSize);
    }

    private void rotationsOnTopSide(AtomicBoolean cubeCorrectness, int cubeSize, Cube cube) throws InterruptedException {
        int threadsCount = 2 * cubeSize;
        int rotations = 1000;
        Thread[] threads = new Thread[threadsCount];
        for (int i = 0; i < threadsCount; i++) {
            int finalI = i % cubeSize;
            threads[i] = new Thread(() -> {
                for (int j = 0; j < rotations; j++) {
                    try {
                        cube.rotate(0, finalI);
                        cubeCorrectness.set(cubeCorrectness.get() && isCubeCorrect(cube));
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                }
            });
        }

        for (Thread t : threads) t.start();
        for (Thread t : threads) t.join();
    }

    // Performing rotations on the same side of the 10-sized cube.
    // Synchronizes 10 threads (each for one layer) before the rotation.
    // After each rotation, checks whether the state of the cube is correct.
    @Test
    public void concurrencyPlusSecurityTest() throws InterruptedException {

        AtomicBoolean cubeCorrectness = new AtomicBoolean(true);
        int cubeSize = 10;
        AtomicInteger counter = new AtomicInteger(0);
        AtomicInteger maxCounter = new AtomicInteger(0);
        CyclicBarrier barrierBeforeRotate = new CyclicBarrier(cubeSize);

        Cube cube = new Cube(cubeSize,
                (x, y) -> { 
                    maxCounter.set(Math.max(maxCounter.get(), counter.incrementAndGet()));
                    try {
                        barrierBeforeRotate.await();
                    } catch (InterruptedException | BrokenBarrierException e) {
                        e.printStackTrace();
                    }
                },
                (x, y) -> { counter.decrementAndGet(); },
                () -> {},
                () -> {});

        rotationsOnTopSide(cubeCorrectness, cubeSize, cube);
        assertEquals(0, counter.get());
        assertTrue(cubeCorrectness.get());
        assertTrue(isCubeCorrect(cube));
        assertTrue(maxCounter.get() <= cubeSize);
    }
    
    // Performs rotations of the same side of the 100-sized cube.
    // Synchronizes 100 threads before each rotation (each for one layer).
    // Checks the amount of threads being in critical section (should be equal to 100 threads each time).
    @Test
    public void concurrencyTest() throws InterruptedException {
        AtomicInteger counter = new AtomicInteger(0);
        int cubeSize = 100;
        AtomicInteger maxCounter = new AtomicInteger(0);
        CyclicBarrier barrierBefore = new CyclicBarrier(cubeSize);
        Cube cube = new Cube(cubeSize,
                (x, y) -> { 
                    maxCounter.set(Math.max(maxCounter.get(), counter.incrementAndGet()));
                    try {
                        barrierBefore.await();
                    } catch (InterruptedException | BrokenBarrierException e) {
                        e.printStackTrace();
                    }
                },
                (x, y) -> { counter.decrementAndGet(); },
                () -> {},
                () -> {});

        int threadsCount = 2 * cubeSize;
        int rotations = 1000;
        Thread[] threads = new Thread[threadsCount];
        for (int i = 0; i < threadsCount; i++) {
            int finalI = i % cubeSize;
            threads[i] = new Thread(() -> {
                for (int j = 0; j < rotations; j++) {
                    try {
                        cube.rotate(0, finalI);
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                }
            });
        }

        for (Thread t : threads) t.start();
        for (Thread t : threads) t.join();

        assertEquals(0, counter.get());
        assertTrue(isCubeCorrect(cube));
        assertTrue(maxCounter.get() == cubeSize);
    }
    
    // Performing infinite amount of rotations on the same side of the 10-sized cube using 20 threads.
    // Meanwhile, one thread is trying to show the cube 10 times.
    // Test checks whether the last thread will get to critical section 10 times.
    @Test
    public void vitalityTest() throws InterruptedException {
        int cubeSize = 10;
        AtomicInteger showCount = new AtomicInteger(0);
        Cube cube = new Cube(cubeSize,
                (x, y) -> { },
                (x, y) -> { },
                showCount::incrementAndGet,
                () -> { });

        int threadsCount = 2 * cubeSize;

        Thread[] threads = new Thread[threadsCount];
        for (int i = 0; i < threadsCount; i++) {
            int finalI = i % cubeSize;
            threads[i] = new Thread(() -> {
                while(true) {
                    try {
                        cube.rotate(0, finalI);
                    } catch (InterruptedException ignored) {
                        
                    }
                }
            });
        }
        
        Thread showThread = new Thread(() -> {
            for (int i = 0; i < 10; i++) {
                try {
                    Thread.sleep(100);
                    cube.show();
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        });

        for (Thread t : threads) t.start();
        showThread.start();
        showThread.join();
        
        for (Thread t : threads) t.interrupt();
        assertEquals(10, showCount.get());
    }

//    // Concurrently performing 2 * 10^6 rotations of the top-side of the 10-sized cube.
//    @Test
//    public void performanceTest() throws InterruptedException, ExecutionException {
//        int cubeSize = 10;
//
//        Cube cube = new Cube(cubeSize,
//                (x, y) -> {},
//                (x, y) -> {},
//                () -> {},
//                () -> {});
//
//        String begin = cube.show();
//        int repeats = 200000;
//        long start = System.currentTimeMillis();
//        Executors.newFixedThreadPool(10).submit(() -> {
//            for (int j = 0; j < repeats; j++) {
//                for (int i = 0; i < cubeSize; i++) {
//                    try {
//                        cube.rotate(0, i);
//                    } catch (InterruptedException e) {
//                        e.printStackTrace();
//                    }
//                }
//            }
//        }).get();
//        long end = System.currentTimeMillis();
//        assertTrue(end - start < 300);
//        assertEquals(cube.show(), begin);
//    }
    
}