MoveType getMoveFromDrag(int faceDir, int cubieIndex, int dragDX, int dragDY) {
        if (cubieIndex < 0 || cubieIndex >= (int)cubies.size()) return MOVE_NONE;
        
        glm::mat4 invView = glm::inverse(viewMatrix);
        glm::vec3 camRight = glm::vec3(invView[0]);
        glm::vec3 camUp = glm::vec3(invView[1]);

        // Calculate world drag vector
        glm::vec3 worldDrag = (float)dragDX * camRight - (float)dragDY * camUp;
        float dragH = 0, dragV = 0;
        
        // Project world drag to face-local 2D coordinates
        switch (faceDir) {
            case POS_Z: dragH = worldDrag.x;  dragV = worldDrag.y; break;
            case NEG_Z: dragH = -worldDrag.x; dragV = worldDrag.y; break;
            case POS_X: dragH = -worldDrag.z; dragV = worldDrag.y; break;
            case NEG_X: dragH = worldDrag.z;  dragV = worldDrag.y; break;
            case POS_Y: dragH = worldDrag.x;  dragV = -worldDrag.z; break;
            case NEG_Y: dragH = worldDrag.x;  dragV = worldDrag.z; break;
        }

        bool horizontal = fabs(dragH) > fabs(dragV);
        int dirH = (dragH > 0) ? 1 : -1;
        int dirV = (dragV > 0) ? 1 : -1;
        int cx = cubies[cubieIndex].x, cy = cubies[cubieIndex].y, cz = cubies[cubieIndex].z;

        // --- Z FACES (Front/Back) ---
        if (faceDir == POS_Z || faceDir == NEG_Z) {
             if (horizontal) {
                 if (cy == 1) return (dirH > 0) ? MOVE_U_PRIME : MOVE_U;
                 if (cy == -1) return (dirH > 0) ? MOVE_D : MOVE_D_PRIME;
                 return (dirH > 0) ? MOVE_E : MOVE_E_PRIME;
             } else {
                 MoveType m;
                 if (cx == 1) m = (dirV > 0) ? MOVE_R : MOVE_R_PRIME;
                 else if (cx == -1) m = (dirV > 0) ? MOVE_L_PRIME : MOVE_L;
                 else m = (dirV > 0) ? MOVE_M_PRIME : MOVE_M;
                 
                 // Fix for Back Face vertical inversion
                 if (faceDir == NEG_Z) {
                     if (m == MOVE_R) m = MOVE_R_PRIME; else if (m == MOVE_R_PRIME) m = MOVE_R;
                     if (m == MOVE_L) m = MOVE_L_PRIME; else if (m == MOVE_L_PRIME) m = MOVE_L;
                     if (m == MOVE_M) m = MOVE_M_PRIME; else if (m == MOVE_M_PRIME) m = MOVE_M;
                 }
                 return m;
             }
        }
        // --- X FACES (Right/Left) ---
        else if (faceDir == POS_X || faceDir == NEG_X) {
            if (horizontal) {
                if (cy == 1) return (dirH > 0) ? MOVE_U_PRIME : MOVE_U;
                if (cy == -1) return (dirH > 0) ? MOVE_D : MOVE_D_PRIME;
                return (dirH > 0) ? MOVE_E : MOVE_E_PRIME;
            } else {
                // FIXED: Vertical logic was inverted here. 
                // Swapped MOVE_F <-> MOVE_F_PRIME, etc.
                MoveType m;
                if (cz == 1) m = (dirV > 0) ? MOVE_F_PRIME : MOVE_F;
                else if (cz == -1) m = (dirV > 0) ? MOVE_B : MOVE_B_PRIME;
                else m = (dirV > 0) ? MOVE_S_PRIME : MOVE_S;

                if (faceDir == NEG_X) {
                    if (m == MOVE_F) m = MOVE_F_PRIME; else if (m == MOVE_F_PRIME) m = MOVE_F;
                    if (m == MOVE_B) m = MOVE_B_PRIME; else if (m == MOVE_B_PRIME) m = MOVE_B;
                    if (m == MOVE_S) m = MOVE_S_PRIME; else if (m == MOVE_S_PRIME) m = MOVE_S;
                }
                return m;
            }
        }
        // --- Y FACES (Up/Down) ---
        else { 
             if (horizontal) {
                MoveType m;
                if (cz == 1) m = (dirH > 0) ? MOVE_F : MOVE_F_PRIME;
                else if (cz == -1) m = (dirH > 0) ? MOVE_B_PRIME : MOVE_B;
                else m = (dirH > 0) ? MOVE_S : MOVE_S_PRIME;

                if (faceDir == NEG_Y) {
                    if (m == MOVE_F) m = MOVE_F_PRIME; else if (m == MOVE_F_PRIME) m = MOVE_F;
                    if (m == MOVE_B) m = MOVE_B_PRIME; else if (m == MOVE_B_PRIME) m = MOVE_B;
                    if (m == MOVE_S) m = MOVE_S_PRIME; else if (m == MOVE_S_PRIME) m = MOVE_S;
                }
                return m;
            } else {
                MoveType m;
                if (cx == 1) m = (dirV > 0) ? MOVE_R : MOVE_R_PRIME;
                else if (cx == -1) m = (dirV > 0) ? MOVE_L_PRIME : MOVE_L;
                else m = (dirV > 0) ? MOVE_M_PRIME : MOVE_M;

                if (faceDir == NEG_Y) {
                    if (m == MOVE_R) m = MOVE_R_PRIME; else if (m == MOVE_R_PRIME) m = MOVE_R;
                    if (m == MOVE_L) m = MOVE_L_PRIME; else if (m == MOVE_L_PRIME) m = MOVE_L;
                    if (m == MOVE_M) m = MOVE_M_PRIME; else if (m == MOVE_M_PRIME) m = MOVE_M;
                }
                return m;
            }
        }
    }