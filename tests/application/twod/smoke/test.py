from stagbl_test_utils import create_stagbl_test

def test() :
    t = create_stagbl_test(__file__,
            'stagbldemo2d',
            1,
            ['-pc_type lu','-pc_factor_mat_solver_package umfpack']
            )

    # Do nothing (test will almost certainly pass)
    def comparefunc(t) :
        pass

    t.setVerifyMethod(comparefunc)

    return(t)
