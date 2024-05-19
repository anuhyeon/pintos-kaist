/* thread/fixed_point.h */
#include <stdint.h>
#define F (1 << 14) // fixed point 1 을 나타낸다. (0 00000000000000001 00000000000000)
#define INT_MAX ((1 << 31) - 1) // INT_MAX 은 (0 11111111111111111 11111111111111)
#define INT_MIN (-(1 << 31)) // INT_MIN 은 (1 00000000000000000 00000000000000)
/*매개변수 x, y 는 fixed-point 형식의 실수이고, n 은 정수를 나타낸다. 모든 mixed 연산에서 n 은 두 번째 파라미터로 들어오게 한다.*/

int int_to_fp (int n) { // int fp_to_int_round () 에서 F / 2 는 fixed point 0.5 를 뜻한다. 0.5 를 더한 후 버림하면 반올림이 된다.
  return n * F;
}

int fp_to_int (int x) {
  return x / F;
}

int fp_to_int_round (int x) {
  if (x >= 0) return (x + F / 2) / F;
  else return (x - F / 2) / F;
}

int add_fp (int x, int y) {
  return x + y;
}

int sub_fp (int x, int y) {
  return x - y;
}

int add_mixed (int x, int n) {
  return x + n * F;
}

int sub_mixed (int x, int n) {
  return x - n * F;
}

int mult_fp (int x, int y) { // int mult_fp () 에서 두 개의 fp 를 곱하면 32bit * 32bit 로 32bit 를 초과하는 값이 발생하기 때문에 x 를 64bit 로 일시적으로 바꿔준 후 곱하기 연산을 하여 오버플로우를 방지하고 계산 결과로 나온 64bit 값을 F 로 나눠서 다시 32bit 값으로 만들어 준다. (div_fp 도 마찬가지)
  return ((int64_t)x) * y / F;
}

int mult_mixed (int x, int n) {
  return x * n;
}

int div_fp (int x, int y) {
  return ((int64_t)x) * F / y;
}

int div_mixed (int x, int n) {
  return x / n;
}