# Decompiled MotionEvent Handler Analysis



## Overview



This function processes an Android `MotionEvent`, normalizes pointer coordinates to a 0–1 range, packs them into a custom event object, and forwards it to another component.



---



## What It Does



1. Determines whether the event is a **press/move (1)** or **release (0)**:

   - `ACTION_DOWN`, `ACTION_POINTER_DOWN`, `ACTION_MOVE` → state = 1  

   - `ACTION_UP`, `ACTION_POINTER_UP` → state = 0  



2. Iterates over active pointers (maximum of 5).



3. For each pointer:

   - Gets the pointer ID.

   - If pointer ID ≥ 5 → abort and return `true`.

   - For `ACTION_POINTER_DOWN` / `ACTION_POINTER_UP`, only processes the pointer at `getActionIndex()`.



4. For each processed pointer:

   - Gets raw X/Y coordinates.

   - Subtracts offsets.

   - Normalizes coordinates into `[0, 1]` using width/height bounds.

   - Clamps values to `[0, 1]`.



5. Stores per-pointer data into a custom container:

   - Normalized X

   - Normalized Y

   - State (1 = down/move, 0 = up)

   - Pointer ID



6. Sends the packed structure to another component.



7. Always returns `true`.



---



## Clean, Readable Version



```java

public static boolean handleMotionEvent(

        MotionEvent event,

        int offsetX,

        int offsetY,

        int width,

        int height

) {

    TouchPacket packet = new TouchPacket();



    int action = event.getActionMasked();



    // Determine press state

    int state;

    if (action == MotionEvent.ACTION_DOWN ||

        action == MotionEvent.ACTION_POINTER_DOWN ||

        action == MotionEvent.ACTION_MOVE) {

        state = 1; // pressed/moving

    } else {

        state = 0; // released

    }



    int pointerCount = Math.min(event.getPointerCount(), 5);



    for (int i = 0; i < pointerCount; i++) {



        int pointerId = event.getPointerId(i);



        // Only support pointer IDs 0–4

        if (pointerId >= 5) {

            return true;

        }



        // If pointer down/up, only handle the changed pointer

        if (action == MotionEvent.ACTION_POINTER_DOWN ||

            action == MotionEvent.ACTION_POINTER_UP) {



            int changedPointerId =

                    event.getPointerId(event.getActionIndex());



            if (pointerId != changedPointerId) {

                continue;

            }

        }



        float rawX = event.getX(i);

        float rawY = event.getY(i);



        int x = (int) rawX - offsetX;

        int y = (int) rawY - offsetY;



        float normX = normalize(x, width);

        float normY = normalize(y, height);



        packet.addPointer(normX, normY, state, pointerId);

    }



    TouchSender.send(packet);

    return true;

}



private static float normalize(int value, int max) {

    if (value <= 0) return 0f;

    if (value >= max) return 1f;

    return (float) value / (float) max;

}