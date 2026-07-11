def RetCC_RISCC : CallingConv<[
  CCIfType<[i1, i8], CCPromoteToType<i16>>,
  CCIfType<[i16], CCAssignToReg<[R1, R2, R3, R4]>>
]>;

def CC_RISCC_Stack : CallingConv<[
  CCIfByVal<CCPassByVal<2, 2>>,
  CCIfType<[i1, i8], CCPromoteToType<i16>>,
  CCIfType<[i16], CCAssignToStack<2, 2>>
]>;
