package concurrentcube;

import java.util.Arrays;
import java.util.concurrent.Semaphore;
import java.util.function.BiConsumer;

public class Cube {
    
    private final int[][][] cube;
    private final int size;
    private final BiConsumer<Integer, Integer> beforeRotation;
    private final BiConsumer<Integer, Integer> afterRotation;
    private final Runnable beforeShowing;
    private final Runnable afterShowing;
    private final Semaphore mutex; // Protecting the global variables.
    private final Semaphore firstInGroup; // Here, representatives of the groups wait.
    private final Semaphore[] rest; // Here, the rest of the group waits.
    private final Semaphore end; // Here, threads that finished critical section wait.
    private int whichGroup; /* Denotes which group is currently in critical section.
                               (-1 means critical section is not being occupied) */
    private int howManyWorking; // Denotes how many threads in a given group are in critical section.
    private final int[] waiting; // Denotes how many threads in each group wait.
    private int howManyEnding; // Denotes how many threads in a currently working group finished critical section.
    private int howManyGroupsWaiting; // Denotes how many groups wait.
    private final Semaphore[] freeLayers; /* Here, threads, that want to move the same layer that is currently
                                             being moved, wait. */

    
    public Cube(int size, 
                BiConsumer<Integer, Integer> beforeRotation, 
                BiConsumer<Integer, Integer> afterRotation, 
                Runnable beforeShowing, 
                Runnable afterShowing) {
        this.size = size;
        this.beforeRotation = beforeRotation;
        this.afterRotation = afterRotation;
        this.beforeShowing = beforeShowing;
        this.afterShowing = afterShowing;
        this.cube = new int[6][size][size];
        
        this.mutex = new Semaphore(1, true);
        this.firstInGroup = new Semaphore(0, true);
        this.end = new Semaphore(0, true);
        this.rest = new Semaphore[4];
        for (int i = 0; i < 4; i++) {
            rest[i] = new Semaphore(0, true);
        }
        this.whichGroup = -1;
        this.howManyWorking = 0;
        this.waiting = new int[4];
        for (int i = 0; i < 4; i++) waiting[i] = 0;
        this.howManyEnding = 0;
        this.howManyGroupsWaiting = 0;
        this.freeLayers = new Semaphore[size];
        for (int i = 0; i < size; i++) {
            freeLayers[i] = new Semaphore(1, true);
        }
        
        for (int i = 0; i < 6; i++) {
            for (int j = 0; j < size; j++) Arrays.fill(cube[i][j], i);
        }
    }

    public int getSize() {
        return size;
    }

    // Returns number of the opposite side of the cube.
    public static int oppSide(int side) {
        switch (side) {
            case 0: return 5;
            case 1: return 3;
            case 2: return 4;
            case 3: return 1;
            case 4: return 2;
            case 5: return 0;
        }
        return -1;
    }
    
    
    private void beginProtocol(int group) throws InterruptedException {
        mutex.acquire();
        if (whichGroup == -1) {
            whichGroup = group;
        }
        else if (whichGroup != group) {
            waiting[group]++;
            if (waiting[group] == 1) { // first in the group
                howManyGroupsWaiting++;
                mutex.release();
                firstInGroup.acquire(); // (1) inheritance of critical section
                howManyGroupsWaiting--;
                whichGroup = group;
            }
            else {
                mutex.release();
                rest[group].acquire(); // (2) inheritance of critical section
            }
            waiting[group]--;
        }

        howManyWorking++;
        if (waiting[group] > 0) {
            rest[group].release(); // (2)
        }
        else {
            mutex.release();
        }
    }
    
    private void endProtocol() throws InterruptedException {
        mutex.acquire();
        howManyWorking--;
        if (howManyWorking > 0) {
            howManyEnding++;
            mutex.release();
            end.acquire(); // (3) inheritance of critical section
            howManyEnding--;
        }

        if (howManyEnding > 0) {
            end.release(); // (3)
        }
        else {
            if (howManyGroupsWaiting > 0) {
                firstInGroup.release(); // (1)
            }
            else {
                whichGroup = -1;
                mutex.release();
            }
        }
    }

    public void rotate(int side, int layer) throws InterruptedException {
        
        int group = Math.min(side, oppSide(side));
        int layerGroup = side == group ? layer : size - 1 - layer;
        
        beginProtocol(group);
        freeLayers[layerGroup].acquire(); // Blocking threads that want to rotate the same layer.
        
        performRotation(side, layer);
        
        freeLayers[layerGroup].release(); // Releasing it after rotation.
        endProtocol();
        
    }

    public void performRotation(int side, int layer) {
        beforeRotation.accept(side, layer);

        switch (side) {
            case 0:
                firstOrLastLayer(side, layer);
                rotateTop(layer);
                break;
            case 1:
                firstOrLastLayer(side, layer);
                rotateLeft(layer);
                break;
            case 2:
                firstOrLastLayer(side, layer);
                rotateFront(layer);
                break;
            case 3:
                firstOrLastLayer(side, layer);
                rotateRight(layer);
                break;
            case 4:
                firstOrLastLayer(side, layer);
                rotateBack(layer);
                break;
            case 5:
                firstOrLastLayer(side, layer);
                rotateBottom(layer);
                break;
        }

        afterRotation.accept(side, layer);
    }

    private void rotateTop(int layer) {
        int[] tmp0 = cube[2][layer];
        cube[2][layer] = cube[3][layer];
        cube[3][layer] = cube[4][layer];
        cube[4][layer] = cube[1][layer];
        cube[1][layer] = tmp0;
    }

    private void rotateBottom(int layer) {
        int[] tmp5 = cube[2][size - 1 - layer];
        cube[2][size - 1 - layer] = cube[1][size - 1 - layer];
        cube[1][size - 1 - layer] = cube[4][size - 1 - layer];
        cube[4][size - 1 - layer] = cube[3][size - 1 - layer];
        cube[3][size - 1 - layer] = tmp5;
    }
    
    private void rotateLeft(int layer) {
        int[] tmp1 = new int[size];
        for (int i = 0; i < size; i++) {
            tmp1[i] = cube[2][i][layer];
            cube[2][i][layer] = cube[0][i][layer];
        }
        for (int i = 0; i < size; i++) cube[0][i][layer] = cube[4][size - 1 - i][size - 1 - layer];
        for (int i = 0; i < size; i++) cube[4][size - 1 - i][size - 1 - layer] = cube[5][i][layer];
        for (int i = 0; i < size; i++) cube[5][i][layer] = tmp1[i];
    }

    private void rotateFront(int layer) {
        int[] tmp2 = new int[size];
        for (int i = 0; i < size; i++) {
            tmp2[i] = cube[0][size - 1 - layer][i];
            cube[0][size - 1 - layer][i] = cube[1][size - 1 - i][size - 1 - layer];
        }
        for (int i = 0; i < size; i++) cube[1][size - 1 - i][size - 1 - layer] = cube[5][layer][size - 1 - i];
        for (int i = 0; i < size; i++) cube[5][layer][size - 1 - i] = cube[3][i][layer];
        for (int i = 0; i < size; i++) cube[3][i][layer] = tmp2[i];
    }

    private void rotateRight(int layer) {
        int[] tmp3 = new int[size];
        for (int i = 0; i < size; i++) {
            tmp3[i] = cube[2][i][size - 1 - layer];
            cube[2][i][size - 1 - layer] = cube[5][i][size - 1 - layer];
        }
        for (int i = 0; i < size; i++) cube[5][i][size - 1 - layer] = cube[4][size - 1 - i][layer];
        for (int i = 0; i < size; i++) cube[4][size - 1 - i][layer] = cube[0][i][size - 1 - layer];
        for (int i = 0; i < size; i++) cube[0][i][size - 1 - layer] = tmp3[i];
    }

    private void rotateBack(int layer) {
        int[] tmp4 = new int[size];
        for (int i = 0; i < size; i++) {
            tmp4[i] = cube[0][layer][i];
            cube[0][layer][i] = cube[3][i][size - 1 - layer];
        }
        for (int i = 0; i < size; i++) cube[3][i][size - 1 - layer] = cube[5][size - 1 - layer][size - 1 - i];
        for (int i = 0; i < size; i++) cube[5][size - 1 - layer][size - 1 - i] = cube[1][size - 1 - i][layer];
        for (int i = 0; i < size; i++) cube[1][size - 1 - i][layer] = tmp4[i];
    }

    private void firstOrLastLayer(int side, int layer) {
        if (layer == 0 || layer == size - 1) {
            int corner0, corner1, corner2, corner3;
            int[] top = new int[size - 2];
            int[] right = new int[size - 2];
            int[] bottom = new int[size - 2];
            int[] left = new int[size - 2];

            if (layer == 0) {
                corner0 = cube[side][0][0];
                corner1 = cube[side][0][size - 1];
                corner2 = cube[side][size - 1][size - 1];
                corner3 = cube[side][size - 1][0];

                cube[side][0][0] = corner3;
                cube[side][0][size - 1] = corner0;
                cube[side][size - 1][size - 1] = corner1;
                cube[side][size - 1][0] = corner2;

                if (size > 2) {
                    for (int i = 1; i < size - 1; i++) top[i - 1] = cube[side][0][i];
                    for (int i = 1; i < size - 1; i++) right[i - 1] = cube[side][i][size - 1];
                    for (int i = 1; i < size - 1; i++) bottom[i - 1] = cube[side][size - 1][size - 1 - i];
                    for (int i = 1; i < size - 1; i++) left[i - 1] = cube[side][size - 1 - i][0];

                    for (int i = 1; i < size - 1; i++) cube[side][0][i] = left[i - 1];
                    for (int i = 1; i < size - 1; i++) cube[side][i][size - 1] = top[i - 1];
                    for (int i = 1; i < size - 1; i++) cube[side][size - 1][size - 1 - i] = right[i - 1];
                    for (int i = 1; i < size - 1; i++) cube[side][size - 1 - i][0] = bottom[i - 1];
                }
            }
            else if (layer == size - 1) {
                corner0 = cube[oppSide(side)][0][0];
                corner1 = cube[oppSide(side)][0][size - 1];
                corner2 = cube[oppSide(side)][size - 1][size - 1];
                corner3 = cube[oppSide(side)][size - 1][0];

                cube[oppSide(side)][0][0] = corner1;
                cube[oppSide(side)][0][size - 1] = corner2;
                cube[oppSide(side)][size - 1][size - 1] = corner3;
                cube[oppSide(side)][size - 1][0] = corner0;

                if (size > 2) {
                    for (int i = 1; i < size - 1; i++) top[i - 1] = cube[oppSide(side)][0][i];
                    for (int i = 1; i < size - 1; i++) right[i - 1] = cube[oppSide(side)][i][size - 1];
                    for (int i = 1; i < size - 1; i++) bottom[i - 1] = cube[oppSide(side)][size - 1][size - 1 - i];
                    for (int i = 1; i < size - 1; i++) left[i - 1] = cube[oppSide(side)][size - 1 - i][0];

                    for (int i = 1; i < size - 1; i++) cube[oppSide(side)][0][i] = right[i - 1];
                    for (int i = 1; i < size - 1; i++) cube[oppSide(side)][i][size - 1] = bottom[i - 1];
                    for (int i = 1; i < size - 1; i++) cube[oppSide(side)][size - 1][size - 1 - i] = left[i - 1];
                    for (int i = 1; i < size - 1; i++) cube[oppSide(side)][size - 1 - i][0] = top[i - 1];
                }
            }
        }
    }

    public String show() throws InterruptedException {

        beginProtocol(3);
        
        beforeShowing.run();
        
        StringBuilder res = new StringBuilder();
        
        for (int i = 0; i < 6; i++) {
            for (int[] row : cube[i]) {
                for (int v : row) res.append(v);
            }
        }
        
        afterShowing.run();
        
        endProtocol();
        
        return res.toString();
    }
}
