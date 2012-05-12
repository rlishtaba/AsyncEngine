#!/usr/bin/env ruby

$LOAD_PATH.insert 0, File.expand_path(File.join(File.dirname(__FILE__), "../../", "lib"))

require "asyncengine"


puts "PID = #{$$}\n\n"
at_exit { puts "NOTICE: exiting..." }


if true and false
  require "benchmark"
  AE.run do
    #puts Benchmark.realtime { 300000.times { AE.add_timer(0.001) {} } }
    puts Benchmark.realtime { 300000.times { a=AE::Timer.new(0.001) {} } }
  end
  exit
end


if true and false
  t1 = Thread.new do
    AE.run { AE.add_periodic_timer(0.1) { printf "." } }
  end

  sleep 0.2
  AE.run { AE.add_periodic_timer(0.1) { printf "-" } }
  t1.join
  exit
end


if true and false
  puts AE.num_handles
  begin
    AE.run do
      AE.add_timer(0.1) { AHH }
      AE.add_timer(1) { puts "AE.run 1 ends" }
    end
  rescue => e
    puts "NOTICE: exception rescued in AE.run: #{e.class} - #{e.message}"
  end
  puts AE.num_handles
  AE.run
  #AE.run { AE.add_timer(0.01) { AHHHHH } } rescue nil
  #AE.run { AE.add_timer(0.1) { puts "FIN"} }
  exit
end


#trap(:INT)  { puts "*** INT trapped => ignore" }
#trap(:INT)  { puts "*** INT trapped => exit" ; exit }


#AE.run { } ; puts "YA" ; exit

#loop do AE.run {} end

#loop do AE.run { AE.add_timer(0) { puts "YA" } } end

#AE.run { AE.add_periodic_timer(0) { puts "---" } } ; exit

#loop do AE.run { AE.next_tick { } } end


if true #and false
  puts "  >>1"
  AE.run { t=AE::Timer.new(0.001) { t.cancel } }

  puts "  >>2"
  t = AE::Timer.new(0.001) { t.cancel } ; AE.run

  puts "  >>3"
  AE.run do
    t=AE::Timer.new(0.001) { printf "." ; t.cancel ; t.restart }
    AE.add_timer(0.3) { t.cancel }
  end

  puts "  >>END"
end


if true and false
  pt = AE::PeriodicTimer.new(0.5,2) { puts "1 - periodic timer fires" }
  AE.add_timer(4) {  pt.restart { puts "2 - periodic timer fires" } }
  AE.add_timer(8) { pt.restart(1) { puts "3 - periodic timer fires" } }
  AE.add_timer(12) { pt.restart(4,2) { puts "4 - periodic timer fires" } }
  AE.add_timer(16) { pt.restart(nil,3) { puts "5 - periodic timer fires" } }
  AE.add_timer(20) { pt.restart(nil,nil) { puts "6 - periodic timer fires" } }

  AE.run
  exit
end


if true and false
  t = AE::Timer.new(0.1) { puts "  - I'm timer" }
  t.restart(2) { puts "  - NEW BLOCK !!!" }
  AE.add_timer(1) { puts t.active? ; t.restart { puts "  - END" } ; puts t.active? }
  AE.run
  exit
end


if true and false
  pt = AE::PeriodicTimer.new(0.001) {}
  100.times do AE.add_periodic_timer(0.001) { pt.restart {} } end
  AE.run
  exit
end


if true and false
  Thread.new { loop { puts "  -t1-" ; sleep 0.001 } }
  Thread.new { loop { puts "  -t2-" ; sleep 0.001 } }
  AE.add_periodic_timer(0.001) { puts "  - ***AE timer 1***" }
  AE.add_periodic_timer(0.001) { puts "  - ***AE timer 2***" }
end


if true and false
  t1 = AE::PeriodicTimer.new(1,0) { puts "  - t1 periodic timer should be stopped after some seconds !!! ---" }
  puts t1.inspect
  AE.add_timer(4) do
    t2 = AE::Timer.new(2) { puts "  - t2 single timer should NOT be stopped !!! ---" }
    puts t2.inspect
    puts "  - canceling t1 ---"
    t1.cancel
    AE.add_timer(1) { puts "exiting..." ; exit }
  end
end


if true and false
  class KK
    def initialize a,b
      @a = a
      @b = b
    end
  end

  require "securerandom"
  $count = 0
  AE.add_periodic_timer(0.5) do
    AE.add_timer(0) do
      kk = KK.new(SecureRandom.hex, (rand*1000).to_i)
      puts "- #{$count += 1}: #{kk.inspect}"
    end
  end
end


if true and false
  cancel = false
  100.times { AE.add_periodic_timer(0.001) { a = AE::Timer.new(0.01){ printf "." } ; ( a.cancel ; a.cancel ) if cancel } }
end



if true #and false
  $interval = 0.1
  pt1 = AE::PeriodicTimer.new($interval) do
    if $interval > 1
      puts "  - pt1: interval > 1, stopping timer"
      pt1.stop
    else
      puts "  - pt1: interval=#{$interval} , setting next interval in #{$interval * 2}"
      pt1.interval = ($interval *= 2)
    end
  end
end


if true #and false
  tt = AE::PeriodicTimer.new(0.1) { puts "LALALA" }
  AE.add_timer(0.2) do
    puts "  - cancel 1 returns #{tt.cancel}"
    puts "  - cancel 2 returns #{tt.cancel}"
    puts "  - set_interval 1 returns #{tt.set_interval 1.1}"
    AE.add_timer(0.3) do
      puts "  - cancel 3 returns #{tt.cancel}"
      puts "  - cancel 4 returns #{tt.cancel}"
      puts "  - set_interval 2 returns #{tt.set_interval 2.2}"
    end
  end
end


if true and false
  AE.run do
    AE.add_timer(0.5) do
      begin
        asd
      rescue => e
        puts "ERROR: exception rescueed: #{e.class} - #{e}"
      end
    end
  end
end


if true #and false
  AE.add_timer(0.001) do
    i = 0
    10.times { AE.next_tick { puts i+=1 } }
    AE.next_tick { puts "A" }
    AE.next_tick { puts "B" ; AE.next_tick { puts "E" } ; puts "C" }
    AE.next_tick { puts "D" ; AE.next_tick { puts "F" ; AE.next_tick { puts "G"  } } }
  end
end



if true and false
  AE.add_periodic_timer(0.001) do
    t = AE::Timer.new(0.2) { puts "first timer expires !!!" }
    AE.add_timer(0.5) do
      puts "second timer canceling first one... which is already ended !!!"
      puts t.cancel
    end
  end
end



#AE.add_periodic_timer(5){ exit }


AE.set_exception_handler {|e| puts "ERROR: exception rescued: #{e.class} - #{e}"} #\n#{e.backtrace.join("\n")}" }
AE.add_timer(0.5) { raise "add_timer: raising an exception !!!" }
AE.next_tick { raise "next_tick: raising an exception !!!" }
#AE.add_timer(0.5) { require "lalala-in-add_timer" }
#AE.next_tick { require "lalala-in-next_tick" }




AE.run do
  puts "\nINFO: starting AsynEngine loop...\n" ; sleep 0.1
  check_timer = AE::PeriodicTimer.new(0.2, 0) do
    if AsyncEngine.num_handles > 1
      puts "DBG: AE.num_handles = #{AsyncEngine.num_handles}"
    else
      #puts "NOTICE: AE.num_handles = #{AsyncEngine.num_handles}, terminating loop..."
      puts "NOTICE: no more alive handles, terminating loop..."
      check_timer.stop
    end
  end
end

AE.run { AE.next_tick { puts "last loop => end" } }