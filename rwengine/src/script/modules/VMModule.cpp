#include <script/modules/VMModule.hpp>
#include <script/ScriptMachine.hpp>
#include <script/SCMFile.hpp>
#include <engine/GameWorld.hpp>
#include <engine/GameState.hpp>
#include <cstring>

SCMThread::pc_t localizeLabel(SCMThread* t, int label)
{
  // Negative jump addresses indicate a jump relative to the start of the thread.
  return (label < 0) ? (t->baseAddress + (-label)) : label;
}

void vm_sleep(const ScriptArguments& args)
{
  args.getThread()->wakeCounter = args[0].integerValue();
  if (args.getThread()->wakeCounter == 0) {
    args.getThread()->wakeCounter = -1;
  }
}

void vm_jump(const ScriptArguments& args)
{
  args.getThread()->programCounter = localizeLabel(args.getThread(), args[0].integer);
}

void vm_global_int(const ScriptArguments& args) { *args[0].globalInteger = args[1].integer; }

void vm_global_float(const ScriptArguments& args) { *args[0].globalReal = args[1].real; }

void vm_inc_global_int(const ScriptArguments& args) { *args[0].globalInteger += args[1].integer; }
void vm_inc_global_float(const ScriptArguments& args) { *args[0].globalReal += args[1].real; }

void vm_dec_global_int(const ScriptArguments& args) { *args[0].globalInteger -= args[1].integer; }
void vm_dec_global_float(const ScriptArguments& args) { *args[0].globalReal -= args[1].real; }

void vm_mul_global_int(const ScriptArguments& args) { *args[0].globalInteger *= args[1].integer; }

void vm_div_global_int(const ScriptArguments& args) { *args[0].globalInteger /= args[1].integer; }
void vm_div_global_float(const ScriptArguments& args) { *args[0].globalReal /= args[1].real; }

void vm_global_int_gt_int(const ScriptArguments& args)
{
  args.getThread()->conditionResult = *args[0].globalInteger > args[1].integer;
}

void vm_int_gt_global_int(const ScriptArguments& args)
{
  args.getThread()->conditionResult = args[0].integer > *args[1].globalInteger;
}

bool vm_global_int_gt_global_int(const ScriptArguments& args)
{
  return *args[0].globalInteger > *args[1].globalInteger;
}

void vm_global_float_gt_float(const ScriptArguments& args)
{
  args.getThread()->conditionResult = *args[0].globalReal > args[1].real;
}

void vm_global_int_ge_int(const ScriptArguments& args)
{
  args.getThread()->conditionResult = *args[0].globalInteger >= args[1].integer;
}

void vm_int_ge_global_int(const ScriptArguments& args)
{
  args.getThread()->conditionResult = args[0].integerValue() >= *args[1].globalInteger;
}

void vm_global_int_eq_int(const ScriptArguments& args)
{
  args.getThread()->conditionResult = *args[0].globalInteger == args[1].integerValue();
}

void vm_global_float_eq_float(const ScriptArguments& args)
{
  args.getThread()->conditionResult = *args[0].globalReal == args[1].real;
}

bool vm_global_int_eq_global_int(const ScriptArguments& args)
{
  return *args[0].globalInteger == *args[1].globalInteger;
}

void vm_new_thread(const ScriptArguments& args) { args.getVM()->startThread(args[0].integer); }

void vm_jump_if_false(const ScriptArguments& args)
{
  if (!args.getThread()->conditionResult) {
    args.getThread()->programCounter = localizeLabel(args.getThread(), args[0].integer);
  }
}

void vm_halt_thread(const ScriptArguments& args)
{
  // ensure the thread is immediately yeilded
  args.getThread()->wakeCounter = -1;
  args.getThread()->finished = true;
}

void vm_call(const ScriptArguments& args)
{
  args.getThread()->calls[args.getThread()->stackDepth++] = args.getThread()->programCounter;
  args.getThread()->programCounter = localizeLabel(args.getThread(), args[0].integer);
}

void vm_return(const ScriptArguments& args)
{
  args.getThread()->programCounter = args.getThread()->calls[--args.getThread()->stackDepth];
}

void vm_inc_global_int_by_global(const ScriptArguments& args)
{
  *args[0].globalInteger += *args[1].globalInteger;
}
void vm_inc_global_float_by_global(const ScriptArguments& args)
{
  *args[0].globalReal += *args[1].globalReal;
}

void vm_dec_global_int_by_global(const ScriptArguments& args)
{
  *args[0].globalInteger -= *args[1].globalInteger;
}

void vm_dec_global_float_by_global(const ScriptArguments& args)
{
  *args[0].globalReal -= *args[1].globalReal;
}

void vm_mul_global_float_by_global(const ScriptArguments& args)
{
  *args[0].globalReal *= *args[1].globalReal;
}

void vm_global_int_to_global(const ScriptArguments& args)
{
  *args[0].globalInteger = *args[1].globalInteger;
}

void vm_global_float_to_global(const ScriptArguments& args)
{
  *args[0].globalReal = *args[1].globalReal;
}

void vm_floor_float_to_int(const ScriptArguments& args)
{
  /// @todo Not sure if this might round to zero
  *args[0].globalInteger = floorf(*args[1].globalReal);
}

void vm_if(const ScriptArguments& args)
{
  auto n = args[0].integer;
  if (n <= 7) {
    args.getThread()->conditionCount = n + 1;
    args.getThread()->conditionMask = 0xFF;
    args.getThread()->conditionAND = true;
  } else {
    args.getThread()->conditionCount = n - 19;
    args.getThread()->conditionMask = 0x00;
    args.getThread()->conditionAND = false;
  }
}

void vm_new_mission_thread(const ScriptArguments& args)
{
  args.getVM()->startThread(args[0].integer, true);
}

void vm_mission_over(const ScriptArguments& args)
{
  for (auto oid : args.getState()->missionObjects) {
    auto obj = args.getWorld()->vehiclePool.find(oid);
    if (obj) {
      args.getWorld()->destroyObjectQueued(obj);
    }
  }

  args.getState()->missionObjects.clear();

  *args.getState()->scriptOnMissionFlag = 0;
}

void vm_sqrt(const ScriptArguments& args) { *args[1].globalReal -= std::sqrt(args[0].real); }

void vm_random_int_in_range(const ScriptArguments& args)
{
  auto min = args[0].integerValue();
  auto max = args[1].integerValue();
  *args[2].globalInteger = std::rand() % (max - min) + min;
}

void vm_name_thread(const ScriptArguments& args)
{
  strncpy(args.getThread()->name, args[0].string, 16);
}

void vm_start_mission(const ScriptArguments& args)
{
  auto offset = args.getVM()->getFile()->getMissionOffsets()[args[0].integer];
  args.getVM()->startThread(offset, true);
}

VMModule::VMModule() : ScriptModule("VM")
{
  bindFunction(0x001, vm_sleep, 1, "Sleep thread");
  bindFunction(0x002, vm_jump, 1, "Jump");

  bindFunction(0x004, vm_global_int, 2, "Set Global Integer");
  bindFunction(0x005, vm_global_float, 2, "Set Global Float");
  // Local Integers are handled by the VM itself, function can be re-used.
  bindFunction(0x006, vm_global_int, 2, "Set Local Int");

  bindFunction(0x008, vm_inc_global_int, 2, "Increment Global Int");
  bindFunction(0x009, vm_inc_global_float, 2, "Increment Global Float");
  bindFunction(0x00C, vm_dec_global_int, 2, "Decrement Global Int");
  bindFunction(0x00D, vm_dec_global_float, 2, "Decrement Global Float");

  bindFunction(0x010, vm_mul_global_int, 2, "Multiply Global Int by Int");

  bindFunction(0x014, vm_div_global_int, 2, "Divide Global by Integer");
  bindFunction(0x015, vm_div_global_float, 2, "Divide Global by Float");

  bindFunction(0x018, vm_global_int_gt_int, 2, "Global Int Greater than Int");
  bindFunction(0x019, vm_global_int_gt_int, 2, "Local Int Greater than Int");

  bindFunction(0x01A, vm_int_gt_global_int, 2, "Int Greater Than Global Int");
  bindFunction(0x01B, vm_int_gt_global_int, 2, "Int Greater Than Var Int");

  bindFunction(0x01F, vm_global_int_gt_global_int, 2, "Local Int Greater than Global Int");
  bindFunction(0x020, vm_global_float_gt_float, 2, "Global Float Greather than Float");

  bindFunction(0x028, vm_global_int_ge_int, 2, "Global Int >= Int");
  bindFunction(0x029, vm_global_int_ge_int, 2, "Local Int >= Int");

  bindFunction(0x02A, vm_int_ge_global_int, 2, "Int >= Global Int");

  bindFunction(0x038, vm_global_int_eq_int, 2, "Global Int Equal to Int");
  bindFunction(0x039, vm_global_int_eq_int, 2, "Local Int Equal to Int");
  bindFunction(0x03A, vm_global_int_eq_global_int, 2, "Global Int Equal to Global Int");

  bindFunction(0x042, vm_global_float_eq_float, 2, "Global Float Equal to Float");

  bindFunction(0x04F, vm_new_thread, -1, "Start New Thread");

  bindFunction(0x04D, vm_jump_if_false, 1, "Jump if false");

  bindFunction(0x04E, vm_halt_thread, 0, "End Thread");

  bindFunction(0x050, vm_call, 1, "Gosub");

  bindFunction(0x051, vm_return, 0, "Return");

  bindFunction(0x058, vm_inc_global_int_by_global, 2, "Increment Global Integer by Global Integer");
  bindFunction(0x059, vm_inc_global_float_by_global, 2, "Increment Global Float by Global Float");

  bindFunction(0x060, vm_dec_global_int_by_global, 2, "Decrement Global Integer by Global Integer");
  bindFunction(0x061, vm_dec_global_float_by_global, 2, "Decrement Global Float by Global Float");

  bindFunction(0x069, vm_mul_global_float_by_global, 2, "Multiply Global Float by Global Float");

  bindFunction(0x084, vm_global_int_to_global, 2, "Set Global Int To Global");

  bindFunction(0x086, vm_global_float_to_global, 2, "Set Global Float To Global");

  bindFunction(0x08C, vm_floor_float_to_int, 2, "Floor Float To Int");

  bindFunction(0x0D6, vm_if, 1, "If");

  bindFunction(0x0D7, vm_new_mission_thread, 1, "Start Mission Thread");

  bindFunction(0x0D8, vm_mission_over, 0, "Set Mission Finished");

  bindFunction(0x1FB, vm_sqrt, 2, "Sqrt");

  bindFunction(0x209, vm_random_int_in_range, 3, "Random Int in Range");

  bindFunction(0x2CD, vm_call, 2, "Call");

  bindFunction(0x3A4, vm_name_thread, 1, "Name Thread");

  bindFunction(0x417, vm_start_mission, 1, "Start Mission");
}
