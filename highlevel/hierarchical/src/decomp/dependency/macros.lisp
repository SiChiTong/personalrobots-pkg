(in-package :dependency-graph)

(defmacro do-children ((c v g) &body body)
  (with-gensyms (e v-var g-var)
    `(let ((,v-var ,v)
	   (,g-var (graph ,g)))
       (do-elements (,e (edges-from ,g-var ,v-var))
	 (let ((,c (dest ,e)))
	   ,@body)))))

(defmacro do-parents ((p v g) &body body)
  (with-gensyms (e v-var g-var)
    `(let ((,v-var ,v)
	   (,g-var (graph ,g)))
       (do-elements (,e (edges-to ,g-var ,v-var))
	 (let ((,p (source ,e)))
	   ,@body)))))
