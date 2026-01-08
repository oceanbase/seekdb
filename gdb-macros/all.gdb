#
# This file includes some gdb scripts.
#
# vim: syntax=gdb


#####################################################################
# ObLinearHashMap gdb scripts.
#
# Includes:
#  - print key value count
#  - print all keys
#  - print all key values
#
# Manual:
#  - include this file by 'source <this-file>' in gdb
#  - find ObLinearHashMap instance
#  - call functions on the map like: 'hashmap-print-kvcnt map'
#
#####################################################################

# --- 1 ---
# Print key value count .
# Syntax: hashmap-print-kvcnt <map>
#
define hashmap-print-kvcnt
  if $argc == 0
    help hashmap-print-kvcnt
  else
    set $cnter = $arg0.cnter_.cnter_
# 64 cnters, cannot get static member from gdb, so use number literal
    set $lmt = 64
    set $cnt = 0
    p $cnt
    set $i = 0
    while $i < $lmt
      set $cnt = $cnt + $cnter[$i].cnt_
      set $i++
    end
    printf "key value count: %u\n", $cnt
  end
end

document hashmap-print-kvcnt
  Print key value count in ObLinearHashMap.
  Syntax: hashmap-print-kvcnt <map>
end

# --- 2 ---
# Print all keys in map.
# Syntax: hashmap-print-allkey <map>
#
define hashmap-print-allkey
  if $argc == 0
    help hashmap-print-allkey
  else
    _linear_hash_map_print_allkv_ $arg0 0
  end
end

document hashmap-print-allkey
  Print all keys stored in ObLinearHashMap.
  Syntax: hashmap-print-allkey <map>
end

# --- 3 ---
# Print all key values in map.
# Syntax: hashmap-print-allkv <map>
#
define hashmap-print-allkv
  if $argc == 0
    help hashmap-print-allkv
  else
    _linear_hash_map_print_allkv_  $arg0 1
  end
end

document hashmap-print-allkv
  Print all key values stored in ObLinearHashMap.
  Syntax: hashmap-print-allkv <map>
end

#
# Print allkv impl.
# Syntax: _linear_hash_map_print_allkv_ <map> <k=0, kv=1>
#
define _linear_hash_map_print_allkv_
  if $argc < 2
    printf "gdb script err\n"
  else
    set $map = $arg0
    set $kv = $arg1
    set $dir = $map.dir_
# Scan all bucket arrays.
    set $arridx = 0
    while $arridx < $map.dir_seg_n_lmt_ && 0 != $dir[$arridx]
      set $bktarr = $dir[$arridx]
      if $arridx < map.m_seg_n_lmt_
        set $bktarr_len = map.m_seg_bkt_n_
      else
        set $bktarr_len = map.s_seg_bkt_n_
      end
#     printf "bkt array %u %p %u\n", $arridx, $bktarr, $bktarr_len
# Scan all buckets.
      set $bktidx = 0
      while $bktidx < $bktarr_len
        set $bkt = $bktarr[$bktidx]
# Print a bucket: flag > 6 means valid bkt.
        if 6 <= $bkt.flag_
          p $bkt.key_
          if 1 == $kv
            p $bkt.value_
          end
          set $curnode = $bkt.next_
          while 0 != $curnode
            p $curnode.key_
            if 1 == $kv
              p $curnode.value_
            end
            set $curnode = $curnode.next_
          end
        end
# Print a bucket end.
        set $bktidx++
      end
# Scan all buckets end.
      set $arridx++
    end
# Scan all bucket arrays end.
  end
end


#####################################################################
# End of ObLinearHashMap gdb script.
#####################################################################




#
define setup
    set pagination off
    set print pretty
end

document setup
Set the gdb options
Syntax: setup
end

define cleanup
    set pagination on
    set print pretty off
end

document cleanup
Reset the gdb options
Syntax: cleanup
end

#
define stack-size
    fr 0
    set $stack_top = $rsp
    up 1000
    p $rsp - $stack_top
end

document stack-size
Print the stack size
Syntax: stack-size
end

#
define find-in-stack
    set $target = $arg0
    fr 0
    set $start = $rsp
    up 1000
    set $end = $rsp
    find $start,$end,$target
    fr 0
end

document find-in-stack
Find in stack
Syntax: find-in-stack <target>
end

#
define find-in-stacks
    set $thread = $arg0
    set $target = $arg1
    set $i = 1
    set $thr = $_thread
    while $i != $thread
        printf "Find in stack for thread %d\n", $i
        set logging file /dev/null
        set logging redirect on
        set logging on
        thread $i
        set logging redirect off
        set logging off
        find-in-stack $target
        set $i = $i + 1
    end
    thread $thr
    fr 0
end

document find-in-stacks
Find in all stacks
Syntax: find-in-all-stack <max-thread-id> <target>
end

#
define dlink
    set $header = (class oceanbase::common::ObDLinkBase<oceanbase::common::ObTimeWheelTask> *) $arg0
    if $header != 0
        set $p = $header->next_
        set $count = 0
        printf "Header: %p\t", $header
        printf "prev: %p\t", $header->prev_
        printf "next: %p\n", $header->next_
        while $p != $header
            printf "Node:   %p\t", $p
            if $p != 0
                printf "prev: %p\t", $p->prev_
            else
                printf "prev: nil"
            end
            if $p != 0
                printf "next: %p\n", $p->next_
            else
                printf "next: nil\n"
                break
            end
            set $count = $count + 1
            set $p = $p->next_
        end
        printf "#Node: %d\n", $count
    end
end

document dlink
Travel the double-linked list
Syntax: dlink <header-address>
end

#
define buckets
    set $i = 0
    while $i != 10001
        printf "bucket: %d\n", $i
        dlink &(this.buckets_[$i].list_.header_)
        set $i = $i + 1
    end
end

document buckets
WARNING: it is terribly slow
Travel all of the buckets used by time wheel.
Syntax: buckets
end


#####################################################################
# Macros for sql engine to show expression tree or oeprator tree:
#
# locatedatum
# showexprtree
# showrawexprtree
# showoptree
# showspectree
#
#####################################################################

define __showexprtree_inner
    set $__expr = $arg0
    set $__i = 0
    while ($__i < $__level)
        echo \ \ \ \ \
        set $__i = $__i + 1
    end

    output $__expr->type_
    echo \ \
    output $__expr->datum_meta_.type_
    echo \ \
    output $__expr
    echo \n

    eval "set $__i%d = 0", $__level
    set $__i = 0
    while ($__i < $__expr->arg_cnt_)
        eval "set $__i%d = $__i", $__level
        eval "set $__expr%d=$__expr", $__level
        set $__level=$__level+1
        __showexprtree_inner $__expr->args_[$__i]
        set $__level=$__level-1
        eval "set $__expr = $__expr%d", $__level
        eval "set $__i = $__i%d", $__level
        set $__i = $__i + 1
    end
end

define showexprtree
    if $argc < 1
        help showexprtree
    else
        set $__level = 0
        __showexprtree_inner $arg0
    end
end

document showexprtree
Show sql expression (ObExpr *) tree.
Usage:
    showexprtree expr_pointer
end

define __show_rawexpr_tree_inner
    set $__expr = $arg0
    set $__i = 0
    while ($__i < $__level)
        echo \ \ \ \ \
        set $__i = $__i + 1
    end

    if 0 != $__expr
		output $__expr->type_
		echo \ \
		output $__expr
	else
		echo 'NULL'
	end
    echo \n

    eval "set $__i%d = 0", $__level
	if 0 != dynamic_cast<oceanbase::sql::ObOpRawExpr *>($__expr)
		set $__i = 0
		while ($__i < $__expr->exprs_.count_)
			eval "set $__i%d = $__i", $__level
			eval "set $__expr%d=$__expr", $__level
			set $__level=$__level+1
			__show_rawexpr_tree_inner $__expr->exprs_.data_[$__i]
			set $__level=$__level-1
			eval "set $__expr = $__expr%d", $__level
			eval "set $__i = $__i%d", $__level
			set $__i = $__i + 1
		end
	end

	if 0 != dynamic_cast<oceanbase::sql::ObCaseOpRawExpr *>($__expr)
		eval "set $__expr%d=$__expr", $__level
		set $__level=$__level+1
		__show_rawexpr_tree_inner $__expr->arg_expr_
		set $__level=$__level-1
		eval "set $__expr = $__expr%d", $__level

		set $__i = 0
		while ($__i < $__expr->when_exprs_.count_)
			eval "set $__i%d = $__i", $__level
			eval "set $__expr%d=$__expr", $__level
			set $__level=$__level+1
			__show_rawexpr_tree_inner $__expr->when_exprs_.data_[$__i]
			set $__level=$__level-1
			eval "set $__expr = $__expr%d", $__level
			eval "set $__i = $__i%d", $__level
			set $__i = $__i + 1
		end

		set $__i = 0
		while ($__i < $__expr->then_exprs_.count_)
			eval "set $__i%d = $__i", $__level
			eval "set $__expr%d=$__expr", $__level
			set $__level=$__level+1
			__show_rawexpr_tree_inner $__expr->then_exprs_.data_[$__i]
			set $__level=$__level-1
			eval "set $__expr = $__expr%d", $__level
			eval "set $__i = $__i%d", $__level
			set $__i = $__i + 1
		end

		eval "set $__expr%d=$__expr", $__level
		set $__level=$__level+1
		__show_rawexpr_tree_inner $__expr->default_expr_
		set $__level=$__level-1
		eval "set $__expr = $__expr%d", $__level
	end
end

define showrawexprtree
    if $argc < 1
        help showrawexprtree
    else
        set $__level = 0
        __show_rawexpr_tree_inner $arg0
    end
end

document showrawexprtree
Show sql expression (ObRawExpr *) tree.
Usage:
    showrawexprtree rawexpr_pointer
end


define locatedatum
    if $argc < 1
        help locatedatum
    else
        set $__expr = (oceanbase::sql::ObExpr *) $arg0
        if $argc > 1
            set $__ctx = $arg1
		else
			set $__ctx = eval_ctx_
        end
        print (oceanbase::common::ObDatum *) ($__ctx.frames_[$__expr->frame_idx_] + $__expr->datum_off_)
    end
end

define locaterevbuf
    if $argc < 1
        help locaterevbuf
    else
        set $__expr = (oceanbase::sql::ObExpr *) $arg0
        if $argc > 1
            set $__ctx = $arg1
		else
			set $__ctx = eval_ctx_
        end
        print ($__ctx.frames_[$__expr->frame_idx_] + $__expr->res_buf_off_)
    end
end

define locatevechead
if $argc < 1
        help locatevechead
    else
        set $__expr = (oceanbase::sql::ObExpr *) $arg0
        if $argc > 1
            set $__ctx = $arg1
		else
			set $__ctx = eval_ctx_
        end
        print (oceanbase::sql::VectorHeader *) ($__ctx.frames_[$__expr->frame_idx_] + $__expr->vector_header_off_)
    end
end

define locatevecformat
if $argc < 1
        help locatevecformat
    else
        set $__expr = (oceanbase::sql::ObExpr *) $arg0
        if $argc > 1
            set $__ctx = $arg1
		else
			set $__ctx = eval_ctx_
        end
        set $__head =  (oceanbase::sql::VectorHeader *) ($__ctx.frames_[$__expr->frame_idx_] + $__expr->vector_header_off_)
        print (*$__head)
    end
end


document locatevechead
Print Vector of ObExpr pointer.
Usage:
    locatevechead expr_pointer [eval_ctx_]
end

document locatevecformat
Print Vector format of ObExpr pointer.
Usage:
    locatevecformat expr_pointer [eval_ctx_]
end

document locatedatum
Print ObDatum of ObExpr pointer.
Usage:
    locatedatum expr_pointer [eval_ctx_]
end

document locaterevbuf
Print ObDatum of ObExpr pointer.
Usage:
    locaterevbuf expr_pointer [eval_ctx_]
end

define locateevalinfo
    if $argc < 1
        help locateevalinfo
    else
        set $__expr = (oceanbase::sql::ObExpr *) $arg0
        if $argc > 1
            set $__ctx = $arg1
		else
			set $__ctx = eval_ctx_
        end
        print (oceanbase::sql::ObEvalInfo *) ($__ctx.frames_[$__expr->frame_idx_] + $__expr->eval_info_off_)
    end
end

document locateevalinfo
Print ObEvalInfo of ObExpr pointer.
Usage:
    locateevalinfo expr_pointer [eval_ctx_]
end

define __showoptree_inner
    set $__op = $arg0
    set $__i = 0
    output $__op->spec_.id_
    while ($__i < $__level)
        echo \ \ \ \ \
        set $__i = $__i + 1
    end

    output $__op->spec_.type_
    echo \ \
    output $__op
    echo \ \
    output &($__op->spec_)
    echo \n

    eval "set $__i%d = 0", $__level
    set $__i = 0
    while ($__i < $__op->child_cnt_)
        eval "set $__i%d = $__i", $__level
        eval "set $__op%d=$__op", $__level
        set $__level=$__level+1
        __showoptree_inner $__op->children_[$__i]
        set $__level=$__level-1
        eval "set $__op = $__op%d", $__level
        eval "set $__i = $__i%d", $__level
        set $__i = $__i + 1
    end
end

define showoptree
    if $argc < 1
        help showoptree
    else
        set $__level = 0
        __showoptree_inner $arg0
    end
end

document showoptree
Show operator tree of the input operator.
Usage:
    showoptree operator_pointer
end

define showoptree-full
    if $argc < 1
        help showoptree-full
    else
        set $__root = $arg0
        while 0 != $__root->parent_
            set $__root = $__root->parent_
        end
        set $__level = 0
        __showoptree_inner $__root
    end
end

document showoptree-full
Locate the root operator and show the root tree.
Usage:
    showoptree-full operator_pointer
end

define __showspectree_inner
    set $__spec = $arg0
    set $__i = 0
    output $__spec->id_
    while ($__i < $__level)
        echo \ \ \ \ \
        set $__i = $__i + 1
    end

    output $__spec->type_
    echo \ \
    output $__spec
    echo \n

    eval "set $__i%d = 0", $__level
    set $__i = 0
    while ($__i < $__spec->child_cnt_)
        eval "set $__i%d = $__i", $__level
        eval "set $__spec%d=$__spec", $__level
        set $__level=$__level+1
        __showspectree_inner $__spec->children_[$__i]
        set $__level=$__level-1
        eval "set $__spec = $__spec%d", $__level
        eval "set $__i = $__i%d", $__level
        set $__i = $__i + 1
    end
end

define showspectree
    if $argc < 1
        help showspectree
    else
        set $__level = 0
        __showspectree_inner $arg0
    end
end

document showspectree
Show operator specification tree of the input operator specification.
Usage:
    showspectree operator_spec_pointer
end

define showspectree-full
    if $argc < 1
        help showspectree-full
    else
        set $__root = $arg0
        while 0 != $__root->parent_
            set $__root = $__root->parent_
        end
        set $__level = 0
        __showspectree_inner $__root
    end
end

document showspectree-full
Locate the root operator specification and show the root tree.
Usage:
    showspectree-full operator_spec_pointer
end

define __showlogoptree_inner
    set $__log_op = $arg0
    set $__i = 0
    output $__log_op->id_
    while ($__i < $__level)
        echo \ \ \ \ \
        set $__i = $__i + 1
    end

    output $__log_op->type_
    echo \ \
    output $__log_op
    echo \n

    eval "set $__i%d = 0", $__level
    set $__i = 0
    while ($__i < $__log_op->child_.count_)
        eval "set $__i%d = $__i", $__level
        eval "set $__log_op%d=$__log_op", $__level
        set $__level=$__level+1
        __showlogoptree_inner $__log_op->child_.data_[$__i]
        set $__level=$__level-1
        eval "set $__log_op = $__log_op%d", $__level
        eval "set $__i = $__i%d", $__level
        set $__i = $__i + 1
    end
end

define showlogoptree
    if $argc < 1
        help showlogoptree
    else
        set $__level = 0
        __showlogoptree_inner $arg0
    end
end

document showlogoptree
Show operator specification tree of the input operator specification.
Usage:
    showlogoptree log_operator_spec_pointer
end

define showlogoptree-full
    if $argc < 1
        help showlogoptree-full
    else
        set $__root = $arg0
        while 0 != $__root->parent_
            set $__root = $__root->parent_
        end
        set $__level = 0
        __showlogoptree_inner $__root
    end
end

document showlogoptree-full
Locate the root operator specification and show the root tree.
Usage:
    showlogoptree-full log_operator_pointer
end

define get_log_plan
    if $argc < 1
        help showspectree-full
    else
        set pagination off
        set $log_plan = $arg0
        set logging file log_plan_temp.txt
        set logging redirect on
        set logging on
        eval "x /%dxb $log_plan.logical_plan_", $log_plan.logical_plan_len_
        set logging off
        set logging redirect off
        shell grep -o ':.*' log_plan_temp.txt  | sed  -E 's/:|0x//g'  |  tr -d '\t\n' > log_plan.txt

        set logging file ob_admin_cmd.sh
        set logging redirect on
        set logging on
        eval "p \"./ob_admin uncompress_plan -u %d -c %d -p 'hex_plan' \" ", $log_plan.uncompress_len_,  $log_plan.logical_plan_len_
        set logging off
        set logging redirect off
        shell sed -i "s/hex_plan/$( cat log_plan.txt )/g" ob_admin_cmd.sh
        shell sed -i "s/\"//g" ob_admin_cmd.sh
        shell cat ob_admin_cmd.sh

        set pagination on
        shell rm -rf log_plan_temp.txt
        shell rm -rf log_plan.txt
        shell rm -rf ob_admin_cmd.sh
    end
end

document get_log_plan
Locate get the log plan str from coredump
Usage:
    get_log_plan ctx_.phy_plan_ctx_.phy_plan_.logical_plan_
end
