# 方案设计

1.工具名

**ob_error**

2.域定义

将所有错误分为三个基本域：

a.OS域：操作系统错误

b.MySQL域：MySQL兼容的错误（MySQL本身存在的错误）

c.OceanBase域：不兼容的OceanBase错误

2.用法

> $ ob_error [option1]
>
> $ ob_error [facility] errorcode

ob_error支持option1：

- `--help`, `-h`
  显示帮助消息并退出。
- `--version`, `-V`
  显示版本信息并退出。



facility用于指定MySQL模式:

- `my,[Mm][Yy]` 

  MySQL模式


errorcode可以有前缀0输入，如00600。



在不输入facility的情况下，从三个基本域中分别搜索对应错误，输出任何存在的错误；

在输入facility的情况下，如果facility为`my`，首先从MySql域搜索错误，如果找到则直接输出MySQL模式错误信息，否则再从OceanBase域搜索。



对于OceanBase域的搜索结果输出为错误代码、错误基本消息、错误原因cause以及错误可能的解决方法solution；

而对于其他域的搜索结果，输出为错误代码、错误基本消息以及相关联的OceanBase内部错误码

1.用例1

```shell
$ ob_error my 4000

OceanBase:
	OceanBase Error Code: OB_ERROR(-4000)
	Message: Common error
	Cause: Internal Error
	Solution: Contact OceanBase Support
```

2.用例2

```shell
$ ob_error 13

Operating System:
	Linux Error Code: EACCES(13)
	Message: Permission denied
```

1.用例3

```shell
$ ob_error 600

OceanBase:
	Error Code 600 not found.
```

2.用例4

```shell
$ ob_error 5858

OceanBase:
	OceanBase Error Code: OB_ERR_CONFLICTING_DECLARATIONS(-5858)
	Message: Conflicting declarations
	Cause: Internal Error
	Solution: Contact OceanBase Support
```

# 方案实现

1.在ob_errno.def内增加C2/C3字段分别表示错误原因和错误解决方法

2.具体错误原因暂时用通用字段“Internal Error”代替，具体解决方法暂时用通用字段“Contact OceanBase Support”代替，由研发同学更新，更新方法可参考ob_errno.def文件的注解，更新后需重新执行gen_ob_errno.pl

1.增加mysql模式错误码到ob错误码的映射关系

2.根据最终映射得到的ob错误码，获取错误信息（含义/原因/解决方法等）

1.实现命令行解析，根据用户输入，输出错误信息



1.任何错误码的更新，只需要对应更新ob_errno.def文件，重新解析生成cpp/h文件后重新编译，映射关系将能够自动更新
